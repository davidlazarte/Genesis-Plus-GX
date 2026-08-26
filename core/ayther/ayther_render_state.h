/* Estado y macros del fork que el renderer Mode 5 consulta. (#43 punto 4)
 *
 * Esto vivia adentro de `vdp_render.c`: 465 lineas en el archivo mas divergente
 * de upstream, que es el que hay que releer entero en cada rebase. No cambia
 * nada de lo que hace el core -- el bloque se incluye en el mismo punto en que
 * estaba, asi que el orden de declaraciones es identico-, pero saca del diff
 * con upstream todo lo que nunca fue de upstream.
 *
 * Que hay aca: las mascaras de control (capa, dim, supresion por sprite, por
 * celda y por plano), los buffers de observacion (atribucion, estado por linea,
 * procedencia por celda), los predicados que deciden si un renderer toma el
 * clon rapido o el observado, y -- en el `#else`-- los stubs que dejan todo eso
 * en cero cuando el fork se compila sin extensiones.
 *
 * NO es un header reutilizable: define objetos, asi que se incluye UNA sola vez,
 * desde `vdp_render.c`. Es un bloque de codigo con nombre, no una interfaz. Lo
 * que el frontend consume vive en `ayther_api.h`; lo que comparten el renderer y
 * el recompositor, en `vdp_render_internal.h`.
 *
 * Requiere `shared.h` y `vdp_render_internal.h` incluidos antes.
 */

#ifndef AYTHER_RENDER_STATE_H
#define AYTHER_RENDER_STATE_H

#ifdef AYTHER_EXTENSIONS

/* #39.C: que le paso a cada sprite de la SAT en este frame. Los bits se
   acumulan -- un sprite alto puede dibujarse arriba y caerse abajo-- y se
   limpian en `vdp_ayther_begin_frame`. */
uint8 ayther_spr_outcome[AYTHER_SPRITE_SAT_SLOTS];

/* Se marca solo cuando alguien mira: el bucle de sprites es codigo caliente
   y este store no tiene por que existir para quien no pidio la region. */
/* #43.4: el preambulo de gates de un renderer de fondo, en UNA linea.
 *
 * Estas seis declaraciones estaban escritas a mano en los cinco renderers
 * Mode 5, en dos formas segun el renderer tuviera clon observado o no. Son 16 y
 * 6 lineas por sitio, todas identicas, todas en `vdp_render.c` -- el archivo mas
 * divergente de upstream y el que hay que releer entero en cada rebase-.
 *
 * Que sean un macro no es solo por el largo. Nada obligaba a que las cinco
 * copias coincidieran, y una que se desviara no fallaria: un renderer que se
 * olvide de `psupW` simplemente no aplica la supresion de Window, sin error y
 * sin sintoma salvo el resultado equivocado. Eso ya paso una vez -- es
 * literalmente lo que #28 vino a arreglar en `render_bg_m5_vs_enhanced`-.
 *
 * Dos variantes porque hay dos clases de renderer:
 *
 *   _OBSERVED  los que tienen clon: el flag es un parametro constante, asi que
 *              el clon rapido borra los gates enteros en compilacion.
 *   (a secas)  los que no lo tienen: leen las mascaras directo.
 */
#define AYTHER_BG_GATES_OBSERVED(obs)                                         \
  const uint8 *psupA = (obs) ? AYTHER_PSUP(0) : (const uint8 *)0;             \
  const uint8 *psupB = (obs) ? AYTHER_PSUP(1) : (const uint8 *)0;             \
  const uint8 *psupW = (obs) ? AYTHER_PSUP(2) : (const uint8 *)0;             \
  const int hide_a = (obs) && !(ayther_layer_mask & AYTHER_LAYER_A);          \
  const int hide_b = (obs) && !(ayther_layer_mask & AYTHER_LAYER_B);          \
  const int hide_w = (obs) && !(ayther_layer_mask & AYTHER_LAYER_W)

#define AYTHER_BG_GATES                                                       \
  const uint8 *psupA = AYTHER_PSUP(0);                                        \
  const uint8 *psupB = AYTHER_PSUP(1);                                        \
  const uint8 *psupW = AYTHER_PSUP(2);                                        \
  const int hide_a = AYTHER_HIDE_A;                                           \
  const int hide_b = AYTHER_HIDE_B;                                           \
  const int hide_w = AYTHER_HIDE_W

/* #43.4: el plano B se borra en vez de dibujarse (ver #37.6). Cinco copias de
   lo mismo, una por renderer. */
#define AYTHER_SKIP_PLANE_B(shift_var, end_var)                               \
  do {                                                                        \
    if (hide_b && !AYTHER_LINE_CELLS_ACTIVE)                                  \
    {                                                                         \
      memset(&linebuf[0][0x20], 0, bitmap.viewport.w);                        \
      (shift_var) = 0;                                                        \
      (end_var) = 0;                                                          \
      AYTHER_METRIC_INC(bg_b_skipped);                                        \
    }                                                                         \
  } while (0)

/* #43.4: el desvio al parser rapido de la SAT, en una linea.
 *
 * La suscripcion habilita, pero lo que obliga al parser completo es que haya
 * ALGO que hacer. SPRITE_CAPTURE si obliga -- el frontend pidio los sprites-.
 * RENDER_CONTROLS no: sin un solo slot suprimido el parser rapido produce
 * exactamente lo mismo, y `test_satb_equiv` (#33) es lo que sostiene esa
 * afirmacion. Antes se pagaba la CAPACIDAD de suprimir en cada linea de cada
 * frame, estuviera o no suprimido algo. */
#define AYTHER_SATB_FAST_PATH(line, im2)                                     \
  do {                                                                       \
    if (!AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE) &&                     \
        !(AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) &&                   \
          ayther_sprite_suppress_active) &&                                  \
        !ayther_rc_nolimit)                                                  \
    {                                                                        \
      parse_satb_m5_fast((line), (im2));                                     \
      return;                                                                \
    }                                                                        \
    AYTHER_METRIC_INC(satb_slow_path);                                       \
  } while (0)

/* #39.C/#43.4: el que no entro por el limite de linea tambien es una
 * respuesta. Se marca este y se sigue caminando la cadena SOLO para marcar los
 * que vienen detras -- sin agregarlos a la lista, que es lo que el hardware
 * hace-. El paseo extra existe unicamente cuando alguien pidio la region: sin
 * suscripcion, el `break` del parser es el de siempre. */
#define AYTHER_SATB_MARK_DROPPED(q, link, total)                             \
  do {                                                                       \
    if (AYTHER_SPR_OUT_ACTIVE)                                               \
    {                                                                        \
      unsigned int rest = (link), left = (total);                            \
      while (left--)                                                         \
      {                                                                      \
        AYTHER_SPR_OUT_MARK(rest >> 2, AYTHER_SPR_OUT_DROP_LINE);            \
        rest = ((q)[rest + 1] & 0x7F) << 2;                                  \
        if ((rest == 0) || (rest >= (unsigned int)bitmap.viewport.w))        \
          break;                                                             \
      }                                                                      \
    }                                                                        \
  } while (0)

/* #43.4: las marcas de resultado por sprite (#39.C), una linea por sitio.
 *
 * Los cuatro renderers Mode 5 las tenian escritas a mano, con su comentario,
 * en cuatro copias identicas cada una: 172 lineas en `vdp_render.c` para tres
 * stores y un barrido. Y estuvieron TRIPLICADAS un rato, porque un patch se
 * aplico dos veces y nada lo noto -- son OR del mismo bit, asi que ni el
 * golden ni un test podian verlo-. Cuatro copias que nada obliga a mantener
 * iguales terminan desviandose; doce, antes.
 */
#define AYTHER_SPR_MARK_LISTED()                                             \
  /* Llego hasta aca, o sea que el parser lo puso en la lista de la linea. */\
  AYTHER_SPR_OUT_MARK(object_info->sat_idx, AYTHER_SPR_OUT_PARSED)

#define AYTHER_SPR_MARK_MASKED()                                             \
  /* `masked` se pega una vez y ya no se suelta: este y todos los que siguen \
     quedan tapados por la mascara de x=0. */                                \
  do {                                                                       \
    if (masked)                                                              \
      AYTHER_SPR_OUT_MARK(object_info->sat_idx, AYTHER_SPR_OUT_MASKED_X0);   \
  } while (0)

#define AYTHER_SPR_MARK_DRAWN()                                              \
  AYTHER_SPR_OUT_MARK(object_info->sat_idx, AYTHER_SPR_OUT_DRAWN)

#define AYTHER_SPR_MARK_PIXEL_LIMIT()                                        \
  /* Los que quedaban en la lista no se dibujan, y saber CUALES es justamente\
     lo que un frontend no puede deducir sin recontar pixeles por linea. El  \
     barrido existe solo si alguien pidio la region. */                      \
  do {                                                                       \
    if (AYTHER_SPR_OUT_ACTIVE)                                               \
    {                                                                        \
      object_info_t *rest = object_info;                                     \
      int left = count + 1;                                                  \
      while (left-- > 0)                                                     \
      {                                                                      \
        AYTHER_SPR_OUT_MARK(rest->sat_idx, AYTHER_SPR_OUT_DROP_PIXEL);       \
        ++rest;                                                              \
      }                                                                      \
    }                                                                        \
  } while (0)

/* #43.4: el gate de supresion de un slot de la SAT, en una linea.
 *
 * Ocultar un sprite es saltear su slot: no se agrega a la lista de la linea,
 * asi que no se dibuja. Solo el frame visible suprime -- la re-sim bare corre
 * con la mascara vacia-, de modo que el status del VDP queda intacto y el
 * replay no deriva.
 *
 * Se marca ademas SUPPRESSED y no un descarte del hardware: "no se dibujo
 * porque vos lo pediste" y "no se dibujo porque el VDP no daba" son respuestas
 * distintas, y un frontend que depura su propio overlay necesita separarlas.
 */
#define AYTHER_SPR_SUPPRESS_GATE(active, slot)                               \
  if (AYTHER_SPR_SUPPRESSED_ACTIVE((active), (slot)))                        \
  {                                                                          \
    AYTHER_SPR_OUT_MARK((slot), AYTHER_SPR_OUT_SUPPRESSED);                  \
  }                                                                          \
  else

#define AYTHER_SPR_OUT_ACTIVE  AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE)
#define AYTHER_SPR_OUT_MARK(idx, bits)                                       \
  do {                                                                       \
    if ((unsigned)(idx) < AYTHER_SPRITE_SAT_SLOTS)                               \
      ayther_spr_outcome[(idx)] |= (uint8)(bits);                            \
  } while (0)

/* AYTHER (#28): ALT_RENDERER trae su propio juego de renderers Mode 5, sin
   ninguno de los gates de este fork. Sólo lo activan GameCube, Wii, GCW0, Vita
   y PSP2 —plataformas que este fork no compila: sus targets son win64, unix y
   osx—, así que la combinación no se despacha en ningún lado.
   Un #error es mejor que un fallback en tiempo de ejecución: la alternativa
   era que alguien portara el fork a una de esas plataformas y descubriera que
   todas las máscaras son no-op en silencio. Si algún día hace falta, lo que
   corresponde es gatear esos renderers, no borrar esta línea. */
#ifdef ALT_RENDERER
#error "AYTHER_EXTENSIONS y ALT_RENDERER son incompatibles: los renderers de ALT_RENDERER no tienen los gates de capa ni de supresion (ver #28)."
#endif

/* AYTHER fork delta: máscara de capas visibles (id de memoria privado 0x102).
   Bit set = visible. La leen render_bg_m5/_vs (planos) y render_line (sprites). */
uint8 ayther_layer_mask = 0xFF;
#define AYTHER_CONTROLS_ACTIVE \
  AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS)
/* #41: que render_line tome el clon OBSERVADO no es lo mismo que que los
   controles esten activos. La atribucion no controla nada -- solo mira-- pero
   se captura adentro de ese clon, asi que suscribirse SOLO a ATTRIBUTION
   dejaba la region vacia para siempre y sin ningun error de por medio: el
   frontend pedia el dato, el core contestaba OK, y devolvia cero bytes.

   Peor todavia con el clon rapido de render_bg, que usa `merge_fast` -- sin
   el hook--: ni siquiera las capas salian, y el frame entero decia
   "backdrop". */
/* La lista tiene que llevar TODA suscripción cuyo dato se capture adentro del
   clon observado. Olvidar una no da un error: da una región que contesta OK y
   viene vacía, que es como se descubrió esto en #41 y como volvió a pasar al
   agregar el estado por línea en #42. Si mañana entra otra región que se llene
   ahí adentro, su bit va en esta lista. */
#define AYTHER_OBSERVED_ACTIVE                    \
  AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS |  \
                    AYTHER_SUB_ATTRIBUTION |      \
                    AYTHER_SUB_LINE_STATE |       \
                    AYTHER_SUB_LINE_CRAM |        \
                    AYTHER_SUB_LINE_CELLS)
#define AYTHER_HIDE_A \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_A))
#define AYTHER_HIDE_B \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_B))
#define AYTHER_HIDE_W \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_W))
#define AYTHER_SHOW_OBJ \
  (!AYTHER_CONTROLS_ACTIVE || (ayther_layer_mask & AYTHER_LAYER_OBJ))

/* AYTHER fork delta: atenuar las capas NO-sprite (id de memoria 0x108, escribible).
   0 = render normal (bit-exact, sin costo). !=0 = "dim mode": los píxeles que NO
   son de sprite se emiten al 25% (preponderancia visual de los sprites — viewport
   de Animación del Lab). Implementación SIN tocar render_obj: render_line snapshotea
   linebuf[0] tras render_bg y lo difea tras render_obj (los píxeles que cambiaron
   son sprites → ayther_sprite_px); remap_line atenúa los demás. Sólo en el frame
   visible (produce); la re-sim bare corre con dim=0. Asume salida RGB565 (el build
   del fork: -DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565). */
uint8 ayther_layer_dim = 0;
static uint8 ayther_bg_snap[0x200];    /* linebuf[0] tras render_bg (fallback Mode 4) */
static uint8 ayther_sprite_px[0x200];  /* 1 = ese pixel es de sprite */

/* AYTHER (#31/#37/#41): "este pixel es de un sprite" se derivaba comparando
   linebuf[0] antes y despues de render_obj. Esa via da dos respuestas malas:

     - un pixel de sprite cuyo byte COINCIDE con el del fondo -- mismo indice,
       misma prioridad-- no aparece en el diff. El dim lo dejaba a brillo pleno
       y la atribucion no lo marcaba: un agujero adentro del sprite;
     - un pixel que solo cambio por el OPERADOR de shadow/highlight aparece, y
       se marcaba como sprite. No lo es: no pone color.

   Ahora se escribe donde se DECIDE, y ese lugar no es uno solo, porque las dos
   familias de renderers de sprites resuelven la prioridad en sitios distintos:

     render_obj_m5 / _im2      dibujan DIRECTO en linebuf[0], y la LUT resuelve
                               sprite-contra-fondo pixel a pixel. La decision
                               esta en el bucle, asi que el store va ahi.
     render_obj_m5_ste / _im2_ste  dibujan en linebuf[1] y mergean despues. La
                               decision esta en el merge, asi que el store va
                               ahi.

   Que sean dos no es duplicacion por gusto: es que la pregunta se contesta en
   dos lugares distintos, y contestarla en el que no decide es exactamente el
   error que el diff cometia. Ninguna de las dos cuesta nada en el perfil
   rapido: el store del bucle vive bajo un parametro de clon que el compilador
   pliega, y el del merge bajo una bandera que solo se enciende con dim o
   atribucion activos. */
static int ayther_obj_pass = 0;      /* 1 = corriendo render_obj              */
static int ayther_obj_px_exact = 0;  /* 1 = alguien lleno ayther_sprite_px    */

/* AYTHER fork delta: CAPTURA de los sprites realmente PARSEADOS por parse_satb
   durante el frame (ids 0x10B lista / 0x10C contador, reset legacy). Algunos
   intros reescriben el SAT in-place a MITAD de frame (el genio del logo Sega: el
   juego carga su SAT justo antes de sus scanlines y lo sobreescribe con placeholders
   después), así que NINGÚN estado del SAT a un instante fijo (fin de frame, línea 0)
   tiene todos los sprites. La única fuente fiable es lo que parse_satb parsea EN SUS
   LÍNEAS. Registramos cada sprite agregado (Y/X/attr ya resueltos + w/h), deduplicado
   por su identidad completa mediante hash fijo O(1) amortizado. La capa libretro
   avanza la generación de captura antes del frame;
   los resets manuales legacy siguen siendo válidos. Produce-only; no afecta render. Valores (no bytes) →
   sin problemas de endianness. */

/* AYTHER fork delta: bitmask de slots SAT suprimidos (id de memoria 0x103,
   escribible). Bit set = ese slot NO se parsea → su sprite no se dibuja (ocultar
   sprite por hash en el Lab). El frontend lo setea SÓLO para el frame visible
   (produce) y lo vacía para la re-simulación bare → status del VDP intacto. */
uint8 ayther_sprite_suppress[16] = {0};
/* #36: resumen de la mascara de arriba, mantenido en write_control. Sin esto,
   estar suscrito a RENDER_CONTROLS bastaba para que parse_satb_m5 tomara el
   parser completo linea por linea aunque no hubiera un solo slot suprimido --
   se pagaba la capacidad de suprimir, no la supresion-. */
uint8 ayther_sprite_suppress_active = 0;
#define AYTHER_SPR_SUPPRESSED_ACTIVE(active, slot) \
  ((active) && \
   (ayther_sprite_suppress[((slot) >> 3) & 0x0F] & (1 << ((slot) & 7))))

/* AYTHER (#270): toggles de la recomposición (ayther_recompose_frame). Sólo esa
   función los enciende, y los apaga antes de volver: el render normal corre
   SIEMPRE con ambos en 0 (cero cambio de comportamiento). */
uint8 ayther_rc_nolimit = 0;  /* sin límite de sprites por línea (20/línea + presupuesto de px) */
uint8 ayther_rc_nomask  = 0;  /* sin máscara de sprites (sprite en x=0 no tapa los siguientes) */

/* AYTHER fork delta: máscara de celdas de tile suprimidas (id de memoria 0x104,
   escribible). Grilla de 8x8 px en coordenadas del frame que ve el frontend
   (col = (x+viewport.x)>>3, fila = (line+viewport.y)>>3); bit set = "ocultar este
   tile". OCULTAR = PELAR UNA CAPA en el merge (no pintar backdrop): si el pixel
   visible vino del primer plano (A/Window) se revela el Plano B de atrás; si vino
   de B se revela el backdrop. Así "ver qué hay detrás" del elemento, no un agujero.
   El frontend mapea el hash de tile oculto → celdas del frame y setea la máscara
   SÓLO para el frame visible (produce); la re-sim bare corre con la máscara vacía.
   Stride fijo de 64 columnas (los modos de MD usan ≤40) → filas alineadas a byte
   (8 bytes/fila) para un descarte rápido por línea. 64x64 celdas = 512 bytes. */
#define AYTHER_TILE_COLS 64
#define AYTHER_TILE_ROWS 64
uint8 ayther_tile_suppress[(AYTHER_TILE_COLS * AYTHER_TILE_ROWS) / 8] = {0};
#define AYTHER_TILE_SUPPRESSED(row, col) \
  (ayther_tile_suppress[(((row) * AYTHER_TILE_COLS) + (col)) >> 3] \
   & (1 << ((((row) * AYTHER_TILE_COLS) + (col)) & 7)))

/* AYTHER fork delta: tiles de PLANO suprimidos por (plano, patrón, paleta) — id de
   memoria 0x105, escribible (Fase 2b del Lab: ocultar un tile de fondo). A diferencia
   de 0x104 (por celda de pantalla), esto oculta el GRÁFICO del tile dondequiera que
   aparezca en su plano, sin depender del scroll: render_bg_m5/_vs lo consultan por
   columna al leer la nametable. 3 planos (A=0, B=1, Window=2) × bitmap de
   (patrón<<2 | paleta) = 2048×4 = 8192 bits = 1024 bytes/plano = 3072 bytes. Identidad
   idéntica a collect_plane_tiles del frontend (patrón = word & 0x7FF, paleta = bits
   13-14). Ocultar un tile de A/Window deja su columna transparente → se ve el Plano B
   (o backdrop); ocultar uno de B → backdrop. Sólo en el frame visible (produce); la
   re-sim bare corre con la máscara vacía (active=0) → determinismo de video intacto. */
uint8 ayther_plane_tile_suppress[3 * 1024] = {0};
uint8 ayther_plane_suppress_active = 0;   /* id 0x106: lo setea el frontend (1 = hay algo oculto) */
/* #37.4: 1 por plano con al menos un tile oculto. Es un RESUMEN del bitmap
   de arriba, no una segunda fuente de verdad: se recalcula siempre desde
   él, nunca se escribe por separado. */
uint8 ayther_psup_any[3] = {0, 0, 0};

void ayther_psup_refresh(void)
{
  int plane;
  if (!ayther_plane_suppress_active)
  {
    ayther_psup_any[0] = ayther_psup_any[1] = ayther_psup_any[2] = 0;
    return;
  }
  for (plane = 0; plane < 3; plane++)
  {
    const uint8 *p = &ayther_plane_tile_suppress[plane * 1024];
    int i, any = 0;
    for (i = 0; i < 1024; i++)
      if (p[i]) { any = 1; break; }
    ayther_psup_any[plane] = (uint8)any;
  }
}
/* AYTHER (#37 punto 4): `ayther_plane_suppress_active` es UN flag para los
   TRES planos. Ocultar un tile del plano A hacía que B y Window también
   perdieran el fast path de DRAW_COLUMN: cada columna de cada línea de esos
   dos planos entraba a `ayther_draw_col` a consultar una máscara enteramente
   vacía. En H40 son ~40 columnas x 224 líneas x 2 celdas = ~18.000 consultas
   por frame y por plano, todas con la misma respuesta "no".

   El issue proponía cambiar el bitmap por una tabla de bytes para ahorrarse
   el armado de la clave: unas 3 operaciones por celda. Esto elimina la
   consulta ENTERA para los planos que nadie tocó, que es donde estaba el
   costo. La tabla de bytes, además, habría costado 6 KB residentes y una
   copia derivada de la región ABI que puede quedar desincronizada de ella:
   memoria y un riesgo de corrección a cambio de un ahorro que este gate
   vuelve irrelevante.

   El resumen se recalcula en `ayther_psup_refresh()`. */
extern uint8 ayther_psup_any[3];
#define AYTHER_PSUP(plane) \
  ((AYTHER_CONTROLS_ACTIVE && ayther_plane_suppress_active && \
    ayther_psup_any[(plane)]) \
    ? &ayther_plane_tile_suppress[(plane) * 1024] : (const uint8 *)0)
/* Bit de una celda de 16 bits (patrón 0x7FF | paleta bits 13-14) dentro del
   bitmap del plano. La clave es (patrón << 2 | paleta), así que el byte es
   patrón >> 1 y el bit ((patrón & 1) << 2 | paleta): se lee sin componer la
   clave para volver a partirla enseguida. */
#define AYTHER_PTSUP(ps, cell)                                             \
  ((ps)[((uint32)(cell) & 0x7FFu) >> 1] &                                  \
   (1u << ((((uint32)(cell) & 1u) << 2) | (((uint32)(cell) >> 13) & 3u))))

/* Estado del "peel" para la línea en curso (lo arma render_line antes de
   render_bg y lo apaga antes de los sprites → no pela los merges de sprites).
   Sólo se activa en líneas con alguna celda marcada → merge() conserva su fast
   path para todos los demás casos/juegos. */
static int ayther_peel_active = 0;   /* la línea actual tiene celdas marcadas */
static int ayther_peel_row    = 0;   /* fila de celda (frame, con borde) de la línea */
static int ayther_peel_vx     = 0;   /* desplazamiento del borde izquierdo (viewport.x) */

/* AYTHER (#41): atribución por píxel — quién pintó cada uno.
 *
 * El frontend deducía esto recomponiendo el frame varias veces con distintas
 * máscaras y diffeando: N pasadas de render para una respuesta que el VDP ya
 * conoce mientras dibuja. Y el atajo que existía —el diff de `layer_dim`— sólo
 * contesta "sprite sí/no", y mal: un píxel de sprite cuyo byte coincide con el
 * del fondo no aparece en el diff.
 *
 * Se captura dentro de `merge()`, que es el punto por el que pasan los CINCO
 * renderers Mode 5. Poner los stores en el bucle interno de cada uno habría
 * significado tocar el código más caliente del emulador cinco veces, y la
 * información que hace falta —qué capa ganó— recién existe en el merge.
 *
 * La capa se decide replicando la regla de prioridad de la LUT, no comparando
 * bytes: dos capas pueden producir el mismo valor y ahí comparar da una
 * respuesta arbitraria justo en los píxeles donde importa. */
/* AYTHER (#42): el estado de render POR SCANLINE.

   El raster journal lo aproxima desde el lado de la ESCRITURA: hay que adivinar
   en que ciclo de que linea cayo cada write y que efecto tuvo. Capturarlo donde
   el renderer lo CONSUME es exacto -- es literalmente el valor que uso-- y ademas
   mas barato: no hay nada que reconstruir.

   240 lineas es el maximo que este core emite (PAL con overscan). El buffer es
   estatico y del tamanio del peor caso: 240 x 32 B de registros mas 240 x 128 B
   de CRAM son 38 KB, del mismo orden que el buffer de atribucion que ya existe,
   y evita una asignacion en un camino que corre por linea. */
#define AYTHER_LINE_MAX 240u
ayther_line_regs_v1 ayther_line_regs[AYTHER_LINE_MAX];
ayther_line_cells_v1 ayther_line_cells[AYTHER_LINE_MAX];
/* #42.C: la fila que se esta capturando, y el plano. El renderer dibuja plano
   por plano, asi que un puntero al arreglo del plano en curso evita pasar el
   plano por cada columna. */
static uint32 *ayther_cell_dst;
static uint8 *ayther_cell_count;
uint8 ayther_line_cram[AYTHER_LINE_MAX][128];
uint32 ayther_line_count = 0;
uint32 ayther_line_flags = 0;
static uint8 ayther_line_cram_first[128];
static int ayther_line_cram_dirty = 0;

#define AYTHER_LINE_STATE_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_STATE)
#define AYTHER_LINE_CRAM_ACTIVE  AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_CRAM)
#define AYTHER_LINE_CELLS_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_CELLS)

/* #42.C: se guarda el par crudo, tal como el renderer lo consumio. Un store a
   un puntero que ya esta en un registro; sin lecturas extra de VRAM. */
#define AYTHER_CELL_RECORD(pair)                                          \
  do {                                                                    \
    if (ayther_observed && ayther_cell_dst &&                             \
        *ayther_cell_count < AYTHER_LINE_CELL_COLUMNS)                    \
      ayther_cell_dst[(*ayther_cell_count)++] = (uint32)(pair);           \
  } while (0)

/* Se llama al empezar el frame: lo que sigue pertenece a ESTE frame y no al
   anterior. `lines` queda en cero hasta que las lineas se dibujen, asi que un
   consumidor que lea a mitad de camino ve lo que hay y no basura vieja. */
void ayther_line_state_begin_frame(void)
{
  ayther_line_count = 0;
  ayther_line_flags = 0;
  ayther_line_cram_dirty = 0;
}

/* La CRAM se copia entera por linea solo si CAMBIO a mitad de frame. En un
   frame sin writes de paleta -- que es la mayoria- se guarda una sola copia y se
   marca CRAM_UNIFORM: 128 B en vez de 30 KB, y el consumidor sabe leerlo. */
static void ayther_line_capture_cram(uint32 line)
{
  if (!ayther_line_cram_dirty)
  {
    if (line == 0)
    {
      memcpy(ayther_line_cram_first, cram, sizeof(ayther_line_cram_first));
      memcpy(ayther_line_cram[0], cram, sizeof(ayther_line_cram[0]));
      return;
    }
    if (!memcmp(ayther_line_cram_first, cram, sizeof(ayther_line_cram_first)))
      return;                      /* sigue igual: no hay nada que guardar */
    /* Cambio: hay que rellenar hacia atras lo que se venia salteando. */
    {
      uint32 i;
      for (i = 1; i < line; ++i)
        memcpy(ayther_line_cram[i], ayther_line_cram_first,
               sizeof(ayther_line_cram[0]));
    }
    ayther_line_cram_dirty = 1;
  }
  memcpy(ayther_line_cram[line], cram, sizeof(ayther_line_cram[0]));
}

/* Capturado a la ENTRADA de render_bg_m5*, con los valores ya resueltos: el
   scroll de la linea, las bases de las tablas y los recortes de la ventana. */
static void ayther_line_capture(uint32 line, uint32 xscroll,
                                                uint32 yscroll)
{
  ayther_line_regs_v1 *r;

  if (line >= AYTHER_LINE_MAX)
  {
    ayther_line_flags |= AYTHER_LINES_OVERFLOW;
    return;
  }
  r = &ayther_line_regs[line];
#ifdef LSB_FIRST
  r->xscroll_a = (uint16)(xscroll & 0x3FF);
  r->xscroll_b = (uint16)((xscroll >> 16) & 0x3FF);
  r->yscroll_a = (uint16)(yscroll & 0x3FF);
  r->yscroll_b = (uint16)((yscroll >> 16) & 0x3FF);
#else
  r->xscroll_a = (uint16)((xscroll >> 16) & 0x3FF);
  r->xscroll_b = (uint16)(xscroll & 0x3FF);
  r->yscroll_a = (uint16)((yscroll >> 16) & 0x3FF);
  r->yscroll_b = (uint16)(yscroll & 0x3FF);
#endif
  r->ntab = ntab; r->ntbb = ntbb; r->ntwb = ntwb;
  r->hscb = hscb; r->satb = satb;
  r->reg1 = reg[1]; r->reg7 = reg[7]; r->reg11 = reg[11]; r->reg12 = reg[12];
  r->reg13 = reg[13]; r->reg16 = reg[16]; r->reg17 = reg[17]; r->reg18 = reg[18];
  r->clip_a_start = (uint8)clip[0].left;
  r->clip_a_end   = (uint8)clip[0].right;
  r->clip_w_start = (uint8)clip[1].left;
  r->clip_w_end   = (uint8)clip[1].right;
  r->flags = (uint8)((clip[1].enable ? AYTHER_LINE_WINDOW_ACTIVE : 0) |
                     ((reg[11] & 0x04) ? AYTHER_LINE_VSCROLL_COLUMN : 0));
  r->reserved0 = 0;

  if (AYTHER_LINE_CRAM_ACTIVE)
  {
    ayther_line_capture_cram(line);
    /* Mientras la CRAM no haya cambiado, la region entrega UNA entrada y este
       flag; el consumidor sabe leerlo. 128 B en vez de 30 KB. */
    if (ayther_line_cram_dirty)
      ayther_line_flags &= ~AYTHER_LINES_CRAM_UNIFORM;
    else
      ayther_line_flags |= AYTHER_LINES_CRAM_UNIFORM;
  }

  if (line + 1u > ayther_line_count)
    ayther_line_count = line + 1u;
}

/* #42.C: abre la captura de un plano para la linea en curso. `row` y `shift`
   son por linea y por plano -- el renderer los calcula una vez-, asi que van
   aca y no repetidos en cada columna. */
static void ayther_cells_open(uint32 line, int plane, uint32 row, uint32 shift)
{
  ayther_line_cells_v1 *c;
  if (line >= AYTHER_LINE_MAX) { ayther_cell_dst = 0; return; }
  c = &ayther_line_cells[line];
  switch (plane)
  {
    case 0: ayther_cell_dst = c->name_a; ayther_cell_count = &c->cols_a;
            c->row_a = (uint8)row; c->shift_a = (uint8)shift; break;
    case 1: ayther_cell_dst = c->name_b; ayther_cell_count = &c->cols_b;
            c->row_b = (uint8)row; c->shift_b = (uint8)shift; break;
    default: ayther_cell_dst = c->name_w; ayther_cell_count = &c->cols_w;
            c->row_w = (uint8)row; break;
  }
  *ayther_cell_count = 0;
}

static void ayther_cells_close(void)
{
  ayther_cell_dst = 0;
}

static void ayther_cells_begin_line(uint32 line)
{
  if (line < AYTHER_LINE_MAX)
    memset(&ayther_line_cells[line], 0, sizeof(ayther_line_cells[line]));
  ayther_cell_dst = 0;
}

uint8 ayther_attrib[320 * 240];
uint32 ayther_attrib_width = 0;
uint32 ayther_attrib_height = 0;
uint32 ayther_attrib_flags = 0;
/* Fila que se está dibujando y si hay que capturar. El flag lo enciende
   `render_line` y NO la recomposición: recomponer es una lectura, y dejar que
   pisara la atribución del frame haría que el resultado dependiera de si
   alguien miró. */
static int ayther_attrib_capture = 0;
static int ayther_attrib_row = 0;
static uint8 ayther_attrib_line[0x200];

#define AYTHER_ATTRIB_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_ATTRIBUTION)

#define AYTHER_LAYER_DIM_ACTIVE \
  (AYTHER_CONTROLS_ACTIVE && ayther_layer_dim)
#define AYTHER_RC_NOLIMIT_ACTIVE ayther_rc_nolimit
#define AYTHER_RC_NOMASK_ACTIVE ayther_rc_nomask

/* Huella de TODO lo que el frontend puede cambiar sin correr un frame (#26).
 *
 * El cache de recomposición se indexaba por (generación de frame, flags,
 * layer_mask). Las otras regiones de control —supresión de sprites, celdas,
 * tiles de plano, dim— también cambian los píxeles que produce el recompositor,
 * y no estaban en la clave: escribir una de ellas entre dos recomposiciones del
 * MISMO frame devolvía la imagen vieja. Silencioso, y peor en el Lab que en
 * ningún lado, porque ahí la secuencia normal es exactamente esa: el emulador
 * pausado y la UI cambiando máscaras sobre un frame fijo.
 *
 * Es una huella de CONTENIDO y no un contador porque los ids privados 0x102-0x108
 * siguen entregando punteros mutables por `retro_get_memory_data`: un consumidor
 * legacy escribe la máscara sin pasar por `write_control`, así que no hay hook
 * donde incrementar nada. Lo único que ve las dos rutas es el contenido.
 *
 * Entra también la máscara de suscripción, porque los controles sólo tienen
 * efecto cuando RENDER_CONTROLS está activa: suscribir o desuscribir cambia el
 * frame recompuesto sin tocar un solo byte de las máscaras.
 *
 * Costo: FNV-1a sobre ~3,6 KB. Es menos de lo que cuesta el memcpy de 150 KB que
 * este mismo cache hace en un acierto, así que no cambia el balance de tenerlo. */
uint64_t ayther_controls_fingerprint(void)
{
  uint64_t h = UINT64_C(0xCBF29CE484222325);
  size_t i;

#define AYTHER_FP_BYTE(b) \
  do { h ^= (uint64_t)(uint8)(b); h *= UINT64_C(0x100000001B3); } while (0)
#define AYTHER_FP_BUF(p, n) \
  do { for (i = 0; i < (size_t)(n); ++i) AYTHER_FP_BYTE(((const uint8 *)(p))[i]); } while (0)

  AYTHER_FP_BYTE(AYTHER_CONTROLS_ACTIVE ? 1 : 0);
  AYTHER_FP_BYTE(ayther_layer_mask);
  AYTHER_FP_BYTE(ayther_layer_dim);
  AYTHER_FP_BYTE(ayther_plane_suppress_active);
  AYTHER_FP_BUF(ayther_sprite_suppress, sizeof(ayther_sprite_suppress));
  AYTHER_FP_BUF(ayther_tile_suppress, sizeof(ayther_tile_suppress));
  AYTHER_FP_BUF(ayther_plane_tile_suppress, sizeof(ayther_plane_tile_suppress));

#undef AYTHER_FP_BUF
#undef AYTHER_FP_BYTE

  return h;
}

#else

#define AYTHER_CONTROLS_ACTIVE 0
#define AYTHER_OBSERVED_ACTIVE 0
#define AYTHER_HIDE_A 0
#define AYTHER_HIDE_B 0
#define AYTHER_HIDE_W 0
#define AYTHER_SHOW_OBJ 1
#define AYTHER_LAYER_DIM_ACTIVE 0
#define AYTHER_RC_NOLIMIT_ACTIVE 0
#define AYTHER_RC_NOMASK_ACTIVE 0
#define AYTHER_SPR_SUPPRESSED_ACTIVE(active, slot) ((void)(active), 0)
#define AYTHER_PSUP(plane) ((const uint8 *)0)
/* #40: render_bg_m4 consulta la supresion por plano; sin extensiones el
   puntero es NULL y la rama es codigo muerto, pero tiene que compilar. */
#define AYTHER_PTSUP(ps, cell) ((void)(ps), (void)(cell), 0)
#define ayther_sprite_capture_record(yr, xr, attr, w, h, sat_idx, chain_pos) ((void)0)
#define AYTHER_CELL_RECORD(pair) ((void)0)
#define AYTHER_LINE_CELLS_ACTIVE 0
/* Sin extensiones los gates son constantes cero: el macro tiene que existir
   igual, porque los renderers lo nombran en los dos perfiles. */
#define AYTHER_BG_GATES_OBSERVED(obs)                                         \
  const uint8 *const psupA = 0;                                               \
  const uint8 *const psupB = 0;                                               \
  const uint8 *const psupW = 0;                                               \
  const int hide_a = ((void)(obs), 0);                                        \
  const int hide_b = 0;                                                       \
  const int hide_w = 0

#define AYTHER_BG_GATES                                                       \
  const uint8 *const psupA = 0;                                               \
  const uint8 *const psupB = 0;                                               \
  const uint8 *const psupW = 0;                                               \
  const int hide_a = 0;                                                       \
  const int hide_b = 0;                                                       \
  const int hide_w = 0

#define AYTHER_SKIP_PLANE_B(shift_var, end_var) ((void)0)

#define AYTHER_SATB_FAST_PATH(line, im2) ((void)0)
#define AYTHER_SATB_MARK_DROPPED(q, link, total) ((void)0)

#define AYTHER_SPR_MARK_LISTED()       ((void)0)
#define AYTHER_SPR_MARK_MASKED()       ((void)0)
#define AYTHER_SPR_MARK_DRAWN()        ((void)0)
#define AYTHER_SPR_MARK_PIXEL_LIMIT()  ((void)0)

#define AYTHER_SPR_SUPPRESS_GATE(active, slot) if (0) { } else

#define AYTHER_SPR_OUT_ACTIVE 0
/* Sin extensiones los bits ni siquiera existen -- viven en ayther_api.h-,
   asi que el stub no puede tocar sus argumentos. */
#define AYTHER_SPR_OUT_MARK(idx, bits) ((void)0)

#endif /* AYTHER_EXTENSIONS */
#endif /* AYTHER_RENDER_STATE_H */
