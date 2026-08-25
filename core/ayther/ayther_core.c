/* AYTHER (#12/#270): el recompositor del fork -- vuelve a renderizar el frame
 * desde el estado FINAL del VDP, con el mismo renderer del core-.
 *
 * #43.2: hasta aca esto entraba al build con un `#include` de este .c al final
 * de vdp_render.c, porque necesitaba sus estaticos. Ahora es una unidad de
 * compilacion propia y el estado compartido se declara en
 * `vdp_render_internal.h`, que es el contrato entre los dos archivos.
 */
#include "shared.h"
#include "vdp_render_internal.h"
#include "ayther_runtime.h"
#include "ayther_metrics.h"

#ifdef AYTHER_EXTENSIONS

extern uint64_t ayther_core_frame_generation;

/* Cache de recomposición (Issue #12) */
static uint64_t ayther_rc_cache_generation = ~(uint64_t)0;
static unsigned int ayther_rc_cache_flags = 0;
static uint8 ayther_rc_cache_mask = 0;
static uint64_t ayther_rc_cache_controls = 0;
static int ayther_rc_cache_valid = 0;
static int ayther_rc_cache_w = 0;
static int ayther_rc_cache_h = 0;
/* #36 punto 7: los caches de recomposicion eran estaticos y sumaban 1,15 MB
   residentes en TODA build con extensions, la mire alguien o no. Un core que
   nadie observa cargaba con eso en memoria y, peor, en cache del procesador:
   1,15 MB de datos que jamas se tocan igual desalojan lineas que si se usan.

   Ahora se piden a malloc la primera vez que alguien recompone, y se sueltan en
   retro_deinit. Un frontend que solo lee VRAM no paga un byte. */
static uint16 *ayther_rc_cache_pixels;

static int ayther_rc_cache_ensure(void)
{
  if (!ayther_rc_cache_pixels)
  {
    ayther_rc_cache_pixels = (uint16 *)malloc(sizeof(uint16) * 320 * 300);
    if (!ayther_rc_cache_pixels) return 0;
  }
  return 1;
}

/* Telemetría de los dos caches (#26). Sin esto, "el cache sigue funcionando"
 * sólo se puede afirmar cronometrando, que en CI es una medición que flakea.
 * Con los contadores el test afirma sobre el mecanismo y no sobre el reloj. */
uint64_t ayther_rc_stat_single_calls = 0;
uint64_t ayther_rc_stat_single_hits = 0;
uint64_t ayther_rc_stat_multi_calls = 0;
uint64_t ayther_rc_stat_multi_hits = 0;

/* Máscara de capas EFECTIVA de una llamada: el byte alto de flags la pisa sólo
 * durante la recomposición. Se calcula una vez y se usa tanto para consultar el
 * cache como para guardarlo; antes la consulta usaba la efectiva y el guardado
 * la ambiente, así que con override el cache no acertaba nunca. */
static uint8 ayther_rc_effective_mask(unsigned int flags)
{
  return (uint8)((flags & AYTHER_RC_LAYER_MASK(0xFF)) ? (flags >> 24)
                                                      : ayther_layer_mask);
}

/* Cache MULTICAPA (tracker viejo 406). El de arriba es de `ayther_recompose_frame` y
 * multilayer no lo tocaba: pedir el mismo frame dos veces costaba lo mismo las
 * dos veces (medido desde el frontend: 0,28 → 0,29 ms). El frontend hace
 * exactamente eso cada vez que el emulador esta en pausa y la UI repinta.
 *
 * `have` es lo que hace que esto sirva con llamadas heterogeneas: una llamada
 * que pide solo el composite guarda solo el composite, y una posterior que pida
 * ademas los planos NO puede servirse del cache. Sin ese bit, el cache
 * devolveria buffers jamas escritos — el defecto de cache clasico, y ademas
 * silencioso.
 *
 * Son 5 x 320x300 x 2 = 960 KB de estaticos. Se paga en el fork, que solo
 * compila para PC; el core stock no incluye este archivo. */
#define AYTHER_ML_A    (1u << 0)
#define AYTHER_ML_B    (1u << 1)
#define AYTHER_ML_W    (1u << 2)
#define AYTHER_ML_SPR  (1u << 3)
#define AYTHER_ML_COMP (1u << 4)

static uint64_t ayther_ml_cache_generation = ~(uint64_t)0;
static unsigned int ayther_ml_cache_flags = 0;
static uint8 ayther_ml_cache_mask = 0;
static uint64_t ayther_ml_cache_controls = 0;
static uint8 ayther_ml_cache_have = 0;
static int ayther_ml_cache_w = 0;
static int ayther_ml_cache_h = 0;
static uint16 (*ayther_ml_cache_px)[320 * 300];

static int ayther_ml_cache_ensure(void)
{
  if (!ayther_ml_cache_px)
  {
    ayther_ml_cache_px = (uint16 (*)[320 * 300])
      malloc(sizeof(uint16) * 5 * 320 * 300);
    if (!ayther_ml_cache_px) return 0;
  }
  return 1;
}

/* Llamado desde retro_deinit: lo que se pidio se suelta. */
void ayther_recompose_release(void)
{
  free(ayther_rc_cache_pixels);
  ayther_rc_cache_pixels = 0;
  free(ayther_ml_cache_px);
  ayther_ml_cache_px = 0;
  ayther_rc_cache_valid = 0;
  ayther_ml_cache_have = 0;
}

/* AYTHER (#37 punto 7): el estado global que una recomposicion pisa y tiene que
 * devolver como estaba.
 *
 * Esto vivia escrito DOS VECES -- una en recompose_frame y otra en
 * recompose_multilayer-, unas 120 lineas casi identicas. No es codigo caliente,
 * asi que no es una cuestion de velocidad: es que una optimizacion o un arreglo
 * aplicado a una sola copia deja la otra silenciosamente distinta, y el sintoma
 * seria un frame corrupto DESPUES de recomponer, lejos de la causa.
 *
 * Recomponer es una LECTURA desde el punto de vista del frontend. Que el core
 * tenga que mover medio mundo para contestarla es un detalle de implementacion
 * que no puede filtrarse al estado del emulador.
 */
typedef struct ayther_render_ctx
{
  uint16 status, spr_col;
  uint8  spr_ovr, layer_mask;
  uint8  object_count[2];
  object_info_t obj_info[2][80];
  struct clip_t clip[2];
  uint8  reg11, reg12, reg18, hscroll_mask;
  uint8 *bitmap_data;
  int    bitmap_pitch, viewport_x, viewport_y;
} ayther_render_ctx;

static void ayther_render_ctx_save(ayther_render_ctx *c)
{
  c->status  = status;
  c->spr_ovr = spr_ovr;
  c->spr_col = spr_col;
  c->layer_mask = ayther_layer_mask;
  memcpy(c->object_count, object_count, sizeof(c->object_count));
  memcpy(c->obj_info, obj_info, sizeof(c->obj_info));
  memcpy(c->clip, clip, sizeof(c->clip));
  c->reg11 = reg[11]; c->reg12 = reg[12]; c->reg18 = reg[18];
  c->hscroll_mask = hscroll_mask;
  c->bitmap_data  = bitmap.data;
  c->bitmap_pitch = bitmap.pitch;
  c->viewport_x = bitmap.viewport.x;
  c->viewport_y = bitmap.viewport.y;

  /* Tiles sucios pendientes al cache, identico al arranque de render_line: el
     proximo frame real haria exactamente este flush. */
  if (bg_list_index)
  {
    update_bg_pattern_cache(bg_list_index);
    bg_list_index = 0;
  }
}

static void ayther_render_ctx_restore(const ayther_render_ctx *c, int sh_rebuilt)
{
  if (sh_rebuilt)
  {
    int i;
    reg[12] = c->reg12;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
  }
  reg[11] = c->reg11;
  reg[12] = c->reg12;
  reg[18] = c->reg18;
  hscroll_mask = c->hscroll_mask;
  memcpy(clip, c->clip, sizeof(c->clip));
  ayther_rc_nolimit = 0;
  ayther_rc_nomask  = 0;
  ayther_layer_mask = c->layer_mask;
  bitmap.data  = c->bitmap_data;
  bitmap.pitch = c->bitmap_pitch;
  bitmap.viewport.x = c->viewport_x;
  bitmap.viewport.y = c->viewport_y;
  memcpy(obj_info, c->obj_info, sizeof(c->obj_info));
  memcpy(object_count, c->object_count, sizeof(c->object_count));
  status  = c->status;
  spr_ovr = c->spr_ovr;
  spr_col = c->spr_col;
}

/* #27: aplicar un cambio de registro durante el raster replay.
 *
 * Escribir `reg[r] = d` no alcanza: el renderer no lee los registros, lee el
 * estado DERIVADO que `vdp_reg_w` calcula al escribirlos (bases de plano,
 * mascara de hscroll, tamano del playfield, recorte del window...). Un replay
 * que solo copiara el byte cambiaria el registro y dibujaria igual que antes,
 * que es la peor de las dos opciones: silencioso y equivocado.
 *
 * Es un espejo de las partes VISUALES de `vdp_reg_w`, sin sus efectos de
 * emulacion (IRQ, redecodificacion de VRAM, `render_line` inmediato,
 * `viewport.changed`): aca no se esta emulando, se esta reconstruyendo una
 * imagen a partir de un estado ya conocido. */
static void ayther_replay_reg(unsigned int r, unsigned int d)
{
  if (r >= 0x20) return;
  reg[r] = (uint8)d;

  switch (r)
  {
    case 2:  ntab = (d << 10) & 0xE000; break;
    case 3:
      ntwb = (d << 10) & ((reg[12] & 1) ? 0xF000 : 0xF800);
      break;
    case 4:  ntbb = (d << 13) & 0xE000; break;
    case 5:
      satb = (d << 9) & ((reg[12] & 1) ? 0xFC00 : 0xFE00);
      break;
    case 7:
    {
      /* El indice de borde y su color; `border` es global justamente para que
         el replay pueda distinguir "cambio un color cualquiera" de "cambio EL
         color del backdrop", que se actualiza por otro camino. */
      int i;
      border = (uint8)(d & 0x3F);
      color_update_m5(0x00, *(uint16 *)&cram[border << 1]);
      (void)i;
      break;
    }
    case 11: hscroll_mask = hscroll_mask_table[d & 0x03]; break;
    case 12:
    {
      /* Solo shadow/highlight. El cambio de H40/H32 no llega hasta aca: se
         declara modo no soportado en el write (mueve el ancho del viewport). */
      int i;
      color_update_m5(0x00, *(uint16 *)&cram[border << 1]);
      for (i = 1; i < 0x40; i++)
        color_update_m5(i, *(uint16 *)&cram[i << 1]);
      window_clip(reg[17], reg[12] & 1);
      break;
    }
    case 13: hscb = (d << 10) & 0xFC00; break;
    case 16:
      playfield_shift    = shift_table[d & 3];
      playfield_col_mask = col_mask_table[d & 3];
      playfield_row_mask = row_mask_table[(d >> 4) & 3];
      break;
    case 17: window_clip(d, reg[12] & 1); break;
    default: break;   /* 0, 1, 6, 8, 9, 18: el renderer los lee de reg[] */
  }
}

int ayther_recompose_frame(uint16 *out, int cap, unsigned int flags,
                           int *out_w, int *out_h)
{
#ifndef USE_16BPP_RENDERING
  (void)out; (void)cap; (void)flags; (void)out_w; (void)out_h;
  return AYTHER_RC_ERR_INVALID_PARAMS;
#else
  ayther_render_ctx ctx;
  int    l, w, h, vs, ste, sh_rebuilt;
  uint64_t rc_controls;
  uint8  rc_mask;
  void (*rbg)(int);
  void (*robj)(int);

  w = bitmap.viewport.w;
  h = bitmap.viewport.h;

  if (!(reg[1] & 0x04)) return AYTHER_RC_ERR_NOT_MODE5;
  if ((reg[12] & 0x06) == 0x06) return AYTHER_RC_ERR_INTERLACE2;
  if (interlaced && config.render) return AYTHER_RC_ERR_INTERLACE2;
  if (config.ntsc) return AYTHER_RC_ERR_NTSC_FILTER;
  if (!out || w <= 0 || h <= 0 || cap < w * h) return AYTHER_RC_ERR_INVALID_PARAMS;

  rc_controls = ayther_controls_fingerprint();
  rc_mask = ayther_rc_effective_mask(flags);
  ayther_rc_stat_single_calls++;

  /* #36.7: el cache se pide la primera vez que alguien recompone. Si no se
     pudo, no hay cache -- se recompone igual, solo que sin memoria. */
  if (!ayther_rc_cache_ensure()) ayther_rc_cache_valid = 0;

  if (ayther_rc_cache_valid && ayther_rc_cache_pixels &&
      ayther_rc_cache_generation == ayther_core_frame_generation &&
      ayther_rc_cache_flags == flags &&
      ayther_rc_cache_mask == rc_mask &&
      ayther_rc_cache_controls == rc_controls &&
      ayther_rc_cache_w == w && ayther_rc_cache_h == h &&
      cap >= w * h)
  {
    memcpy(out, ayther_rc_cache_pixels, w * h * 2);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    ayther_rc_stat_single_hits++;
    return 1;
  }

  /* ---- salvar todo lo que el render muta ---- */
  ayther_render_ctx_save(&ctx);

  /* ---- aplicar flags ---- */
  if (flags & AYTHER_RC_FLAT_HS)
  {
    /* toda línea lee la entrada 0 de la tabla (= el valor de la línea 0): lo
       que vería un render sin scroll raster. OJO: el fetch del hscroll usa
       hscroll_mask (`line & hscroll_mask`, cache del write de reg 11), NO
       reg[11] — clampear el registro acá sería un no-op silencioso. */
    hscroll_mask = 0x00;
  }
  if (flags & AYTHER_RC_NO_WINDOW)
  {
    /* window a tamaño 0 (el plano A ocupa toda la línea); clip[] se restaura
       por memcpy al salir */
    window_clip(0, reg[12] & 1);
    reg[18] = 0;
  }
  ayther_rc_nolimit = (flags & AYTHER_RC_NO_SPR_LIMIT) ? 1 : 0;
  ayther_rc_nomask  = (flags & AYTHER_RC_NO_SPR_MASK)  ? 1 : 0;

  /* R-2: override de la máscara de capas SÓLO durante la recomposición (byte
     alto de flags; 0 = mantener la vigente). Recomponer "solo el plano B sobre
     backdrop" es el oráculo CPU contra el que se valida el pipeline indexado
     de la GPU — mismo renderer que midió el spike R-1. */
  if (flags & AYTHER_RC_LAYER_MASK(0xFF))
    ayther_layer_mask = (uint8)(flags >> 24);

  vs  = (reg[11] & 0x04) && !(flags & AYTHER_RC_FLAT_VS);
  ste = (reg[12] & 0x08) && !(flags & AYTHER_RC_NO_SH);

  sh_rebuilt = 0;
  if ((reg[12] & 0x08) && !ste)
  {
    /* apagar shadow/highlight: reconstruir pixel[] como si reg12.3 = 0
       (mismo patrón que el write de reg 12 en vdp_ctrl.c) */
    int i;
    reg[12] &= ~0x08;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
    sh_rebuilt = 1;
  }

  rbg  = vs ? (config.enhanced_vscroll ? render_bg_m5_vs_enhanced
                                       : render_bg_m5_vs)
            : render_bg_m5;
  robj = ste ? render_obj_m5_ste : render_obj_m5;

  /* ---- el frame se escribe al buffer del caller ---- */
  bitmap.data  = (uint8 *)out;
  bitmap.pitch = w * 2;
  bitmap.viewport.x = 0;   /* sin bordes en el buffer de salida */
  bitmap.viewport.y = 0;

  /* obj_info de la línea 0, parseado del estado final (line=-1 → offset
     0x81-1 = 0x80, el Y crudo de la línea 0 — mismo camino que el parse
     de fin de vblank) */
  parse_satb_m5(-1);

  for (l = 0; l < h; l++)
  {
    if (reg[1] & 0x40)
    {
      rbg(l);
      /* mismo guard que render_line: la máscara de capas también apaga los
         sprites (con el override de R-2, "solo plano B" es SOLO plano B) */
      if (ayther_layer_mask & AYTHER_LAYER_OBJ)
        robj(l & 1);

      /* left-most column blanking (reg 0 bit 5) */
      if (reg[0] & 0x20)
        memset(&linebuf[0][0x20], 0x40, 8);

      /* sprites de la línea siguiente, del mismo estado final */
      if (l < h - 1)
        parse_satb_m5(l);
    }
    else
    {
      /* display off (estado final) → línea al backdrop */
      memset(&linebuf[0][0x20], 0x40, w);
    }
    remap_line(l);
  }

  /* ---- restaurar ---- */
  ayther_render_ctx_restore(&ctx, sh_rebuilt);

  if (out_w) *out_w = w;
  if (out_h) *out_h = h;

  if (w * h <= 320 * 300)
  {
    /* La huella y la máscara son las de la ENTRADA: durante el render se pisan
       (`ayther_layer_mask` con el override, y el frontend no puede escribir
       porque `write_control` rechaza con BUSY), y acá ya se restauraron. Guardar
       lo de la entrada es lo que hace que la próxima consulta con el mismo
       estado acierte. */
    ayther_rc_cache_generation = ayther_core_frame_generation;
    ayther_rc_cache_flags = flags;
    ayther_rc_cache_mask = rc_mask;
    ayther_rc_cache_controls = rc_controls;
    ayther_rc_cache_w = w;
    ayther_rc_cache_h = h;
    ayther_rc_cache_valid = 1;
    if (ayther_rc_cache_pixels)
      memcpy(ayther_rc_cache_pixels, out, w * h * 2);
  }
  else
  {
    ayther_rc_cache_valid = 0;
  }

  return 1;
#endif
}

int ayther_core_recompose_multilayer(
    uint16 *out_bg_a, uint16 *out_bg_b, uint16 *out_window,
    uint16 *out_sprites, uint16 *out_composite,
    int cap, unsigned int flags,
    int *out_w, int *out_h)
{
#ifndef USE_16BPP_RENDERING
  return AYTHER_RC_ERR_INVALID_PARAMS;
#else
  ayther_render_ctx ctx;
  int    l, w, h, vs, ste, sh_rebuilt;
  uint8  ml_want = 0;
  uint64_t ml_controls;
  void (*rbg)(int);
  void (*robj)(int);

  uint16 s_cram[64];
  uint8 s_regs[0x20];
  uint8 s_vsram[0x80];
  /* #27: undo-log EXACTO de las words de VRAM que toca el replay, en vez de
     salvar la ventana de 1 KB de la tabla de hscroll. Si un evento de reg 13
     mueve `hscb` a mitad de frame, la ventana salvada y la ventana escrita ya
     no son la misma, y restaurar la primera dejaba el frame con VRAM sucia en
     dos lugares. Un log por escritura no depende de donde este la base. */
  uint16 s_vram_undo_addr[AYTHER_RASTER_JOURNAL_MAX];
  uint16 s_vram_undo_val[AYTHER_RASTER_JOURNAL_MAX];
  int    s_vram_undo_n = 0;
  /* #27: el replay de registros toca el estado DERIVADO del VDP, no solo
     `reg[]`. Todo lo que `ayther_replay_reg` puede mover se salva aca; que la
     recomposicion no perturbe la emulacion es la propiedad de la que dependen
     los tres pases del replay determinista. */
  uint16 s_ntab, s_ntbb, s_ntwb, s_satb, s_hscb, s_pf_row;
  uint8  s_pf_shift, s_pf_col, s_border;

  w = bitmap.viewport.w;
  h = bitmap.viewport.h;

  if (!(reg[1] & 0x04)) return AYTHER_RC_ERR_NOT_MODE5;
  if ((reg[12] & 0x06) == 0x06) return AYTHER_RC_ERR_INTERLACE2;
  if (interlaced && config.render) return AYTHER_RC_ERR_INTERLACE2;
  if (config.ntsc) return AYTHER_RC_ERR_NTSC_FILTER;
  if (w <= 0 || h <= 0 || cap < w * h) return AYTHER_RC_ERR_INVALID_PARAMS;
  /* #27: con el journal desbordado solo se puede reproducir un prefijo del
     frame. Un prefijo produce una imagen que parece correcta, que es
     exactamente lo que un frontend no puede detectar por su cuenta. */
  if (ayther_raster_journal_dropped > 0)
    return AYTHER_RC_ERR_JOURNAL_OVERFLOW;

  /* ---- cache (tracker viejo 406): mismo frame, misma configuracion ---- */
  {
    const uint8 want = (uint8)((out_bg_a     ? AYTHER_ML_A    : 0) |
                               (out_bg_b     ? AYTHER_ML_B    : 0) |
                               (out_window   ? AYTHER_ML_W    : 0) |
                               (out_sprites  ? AYTHER_ML_SPR  : 0) |
                               (out_composite? AYTHER_ML_COMP : 0));
    ml_want = want;
    ml_controls = ayther_controls_fingerprint();
    ayther_rc_stat_multi_calls++;
    if (!ayther_ml_cache_ensure()) ayther_ml_cache_have = 0;
    if (ayther_ml_cache_px &&
        ayther_ml_cache_generation == ayther_core_frame_generation &&
        ayther_ml_cache_flags == flags &&
        ayther_ml_cache_mask == ayther_layer_mask &&
        ayther_ml_cache_controls == ml_controls &&
        ayther_ml_cache_w == w && ayther_ml_cache_h == h &&
        (want & ~ayther_ml_cache_have) == 0)
    {
      const int n = w * h * 2;
      if (out_bg_a)      memcpy(out_bg_a,      ayther_ml_cache_px[0], n);
      if (out_bg_b)      memcpy(out_bg_b,      ayther_ml_cache_px[1], n);
      if (out_window)    memcpy(out_window,    ayther_ml_cache_px[2], n);
      if (out_sprites)   memcpy(out_sprites,   ayther_ml_cache_px[3], n);
      if (out_composite) memcpy(out_composite, ayther_ml_cache_px[4], n);
      if (out_w) *out_w = w;
      if (out_h) *out_h = h;
      ayther_rc_stat_multi_hits++;
      return 1;
    }
  }

  ayther_render_ctx_save(&ctx);

  if (flags & AYTHER_RC_FLAT_HS) hscroll_mask = 0x00;
  if (flags & AYTHER_RC_NO_WINDOW)
  {
    window_clip(0, reg[12] & 1);
    reg[18] = 0;
  }
  ayther_rc_nolimit = (flags & AYTHER_RC_NO_SPR_LIMIT) ? 1 : 0;
  ayther_rc_nomask  = (flags & AYTHER_RC_NO_SPR_MASK)  ? 1 : 0;


  vs  = (reg[11] & 0x04) && !(flags & AYTHER_RC_FLAT_VS);
  ste = (reg[12] & 0x08) && !(flags & AYTHER_RC_NO_SH);

  sh_rebuilt = 0;
  if ((reg[12] & 0x08) && !ste)
  {
    int i;
    reg[12] &= ~0x08;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
    sh_rebuilt = 1;
  }

  rbg  = vs ? (config.enhanced_vscroll ? render_bg_m5_vs_enhanced : render_bg_m5_vs) : render_bg_m5;
  robj = ste ? render_obj_m5_ste : render_obj_m5;

  bitmap.pitch = w * 2;
  bitmap.viewport.x = 0;
  bitmap.viewport.y = 0;

  parse_satb_m5(-1);

  memcpy(s_cram, cram, sizeof(cram));
  memcpy(s_regs, reg, sizeof(reg));
  memcpy(s_vsram, vsram, sizeof(vsram));
  s_ntab = ntab; s_ntbb = ntbb; s_ntwb = ntwb; s_satb = satb; s_hscb = hscb;
  s_pf_shift = playfield_shift; s_pf_col = playfield_col_mask;
  s_pf_row = playfield_row_mask; s_border = border;

  for (l = 0; l < h; l++)
  {
    if (ayther_raster_journal_count > 0)
    {
      int ev_idx;
      for (ev_idx = 0; ev_idx < ayther_raster_journal_count; ++ev_idx)
      {
        ayther_raster_event_t *ev = &ayther_raster_journal[ev_idx];
        if (ev->v_counter == l)
        {
          if (ev->reason == AYTHER_RASTER_REASON_CRAM)
          {
            uint16 *p = (uint16 *)&cram[ev->address & 0x7E];
            int index = (ev->address >> 1) & 0x3F;
            *p = ev->data;
            if (index & 0x0F) color_update_m5(index, ev->data);
            if (index == border) color_update_m5(0x00, ev->data);
          }
          else if (ev->reason == AYTHER_RASTER_REASON_VSRAM)
          {
            uint16 *p = (uint16 *)&vsram[ev->address & 0x7E];
            *p = ev->data;
          }
          else if (ev->reason == AYTHER_RASTER_REASON_REG)
          {
            ayther_replay_reg(ev->address, ev->data);
          }
          else if (ev->reason == AYTHER_RASTER_REASON_HSCROLL)
          {
            /* #27: la tabla de hscroll se escribe por WORDS (el evento guarda
               los 16 bits que el bus puso), y esto aplicaba solo el byte bajo.
               El byte alto se perdia, asi que el scroll reproducido no era el
               que el juego habia programado. Se usa la misma expresion que el
               emulador para respetar el layout interno word-swapped de vram. */
            uint16 *p = (uint16 *)&vram[ev->address & 0xFFFE];
            if (s_vram_undo_n < AYTHER_RASTER_JOURNAL_MAX)
            {
              s_vram_undo_addr[s_vram_undo_n] = (uint16)(ev->address & 0xFFFE);
              s_vram_undo_val[s_vram_undo_n] = *p;
              s_vram_undo_n++;
            }
            *p = ev->data;
          }
          /* Un motivo que el replay no sabe reproducir NO se anota aca: esta
             funcion es de solo lectura por contrato y `ayther_raster_dirty` es
             el estado publico del frame. Lo que no es reproducible ya se
             rechazo antes de entrar (AYTHER_RASTER_REASON_REPLAYABLE). */
        }
      }
    }
    if (reg[1] & 0x40)
    {
      if (out_bg_b)
      {
        ayther_layer_mask = AYTHER_LAYER_B;
        bitmap.data = (uint8 *)out_bg_b;
        rbg(l);
        remap_line(l);
      }
      if (out_bg_a)
      {
        ayther_layer_mask = AYTHER_LAYER_A;
        bitmap.data = (uint8 *)out_bg_a;
        rbg(l);
        remap_line(l);
      }
      if (out_window)
      {
        ayther_layer_mask = AYTHER_LAYER_W;
        bitmap.data = (uint8 *)out_window;
        rbg(l);
        remap_line(l);
      }
      if (out_sprites)
      {
        ayther_layer_mask = AYTHER_LAYER_OBJ;
        bitmap.data = (uint8 *)out_sprites;
        rbg(l);
        robj(l & 1);
        remap_line(l);
      }
      if (out_composite)
      {
        ayther_layer_mask = AYTHER_LAYER_A | AYTHER_LAYER_B | AYTHER_LAYER_W | AYTHER_LAYER_OBJ;
        bitmap.data = (uint8 *)out_composite;
        rbg(l);
        robj(l & 1);
        remap_line(l);
      }

      if (reg[0] & 0x20)
        memset(&linebuf[0][0x20], 0x40, 8);

      if (l < h - 1)
        parse_satb_m5(l);
    }
    else
    {
      int b;
      uint8 *buffers[] = { (uint8 *)out_bg_a, (uint8 *)out_bg_b, (uint8 *)out_window, (uint8 *)out_sprites, (uint8 *)out_composite };
      for (b = 0; b < 5; b++)
      {
        if (buffers[b])
        {
          bitmap.data = buffers[b];
          memset(&linebuf[0][0x20], 0x40, w);
          remap_line(l);
        }
      }
    }
  }

  if (sh_rebuilt || ayther_raster_journal_count > 0)
  {
    int i;
    if (ayther_raster_journal_count > 0)
    {
      /* En orden inverso: si el frame escribio dos veces la misma word, el
         valor bueno es el que habia antes de la PRIMERA. */
      while (s_vram_undo_n > 0)
      {
        --s_vram_undo_n;
        *(uint16 *)&vram[s_vram_undo_addr[s_vram_undo_n]] =
          s_vram_undo_val[s_vram_undo_n];
      }
      memcpy(cram, s_cram, sizeof(cram));
      memcpy(reg, s_regs, sizeof(reg));
      memcpy(vsram, s_vsram, sizeof(vsram));
      ntab = s_ntab; ntbb = s_ntbb; ntwb = s_ntwb; satb = s_satb;
      hscb = s_hscb;
      playfield_shift = s_pf_shift; playfield_col_mask = s_pf_col;
      playfield_row_mask = s_pf_row; border = s_border;
    }
    reg[12] = ctx.reg12;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
  }
  ayther_render_ctx_restore(&ctx, 0);

  /* ---- guardar en el cache (tracker viejo 406) ----
     Si la clave cambio, lo guardado antes ya no vale y `have` arranca de cero:
     acumular sobre datos de otro frame es exactamente el bug que este cache
     podria introducir. Si la clave es la misma, se SUMAN las capas nuevas a las
     que ya habia. */
  if (w * h <= 320 * 300)
  {
    const int n = w * h * 2;
    if (ayther_ml_cache_generation != ayther_core_frame_generation ||
        ayther_ml_cache_flags != flags ||
        ayther_ml_cache_mask != ayther_layer_mask ||
        ayther_ml_cache_controls != ml_controls ||
        ayther_ml_cache_w != w || ayther_ml_cache_h != h)
    {
      ayther_ml_cache_generation = ayther_core_frame_generation;
      ayther_ml_cache_flags = flags;
      ayther_ml_cache_mask = ayther_layer_mask;
      ayther_ml_cache_controls = ml_controls;
      ayther_ml_cache_w = w;
      ayther_ml_cache_h = h;
      ayther_ml_cache_have = 0;
    }
    if (ayther_ml_cache_px)
    {
      if (out_bg_a)      memcpy(ayther_ml_cache_px[0], out_bg_a,      n);
      if (out_bg_b)      memcpy(ayther_ml_cache_px[1], out_bg_b,      n);
      if (out_window)    memcpy(ayther_ml_cache_px[2], out_window,    n);
      if (out_sprites)   memcpy(ayther_ml_cache_px[3], out_sprites,   n);
      if (out_composite) memcpy(ayther_ml_cache_px[4], out_composite, n);
      ayther_ml_cache_have |= ml_want;
    }
  }

  if (out_w) *out_w = w;
  if (out_h) *out_h = h;
  return 1;
#endif
}

#endif /* AYTHER_EXTENSIONS */
