#ifdef AYTHER_EXTENSIONS

extern uint64_t ayther_core_frame_generation;

/* Cache de recomposición (Issue #12) */
static uint64_t ayther_rc_cache_generation = ~(uint64_t)0;
static unsigned int ayther_rc_cache_flags = 0;
static uint8 ayther_rc_cache_mask = 0;
static uint16 ayther_rc_cache_pixels[320 * 300];

int ayther_recompose_frame(uint16 *out, int cap, unsigned int flags,
                           int *out_w, int *out_h)
{
#ifndef USE_16BPP_RENDERING
  (void)out; (void)cap; (void)flags; (void)out_w; (void)out_h;
  return AYTHER_RC_ERR_INVALID_PARAMS;
#else
  uint16 s_status, s_sprcol;
  uint8  s_sprovr, s_lmask;
  uint8  s_objcount[2];
  object_info_t s_objinfo[2][80];
  struct clip_t s_clip[2];
  uint8  s_reg11, s_reg12, s_reg18, s_hsmask;
  uint8 *s_bdata;
  int    s_bpitch, s_bvx, s_bvy;
  int    l, w, h, vs, ste, sh_rebuilt;
  void (*rbg)(int);
  void (*robj)(int);

  w = bitmap.viewport.w;
  h = bitmap.viewport.h;

  if (!(reg[1] & 0x04)) return AYTHER_RC_ERR_NOT_MODE5;
  if ((reg[12] & 0x06) == 0x06) return AYTHER_RC_ERR_INTERLACE2;
  if (interlaced && config.render) return AYTHER_RC_ERR_INTERLACE2;
  if (config.ntsc) return AYTHER_RC_ERR_NTSC_FILTER;
  if (!out || w <= 0 || h <= 0 || cap < w * h) return AYTHER_RC_ERR_INVALID_PARAMS;

  if (ayther_rc_cache_generation == ayther_core_frame_generation &&
      ayther_rc_cache_flags == flags &&
      ayther_rc_cache_mask == (uint8)((flags & AYTHER_RC_LAYER_MASK(0xFF)) ? (flags >> 24) : ayther_layer_mask) &&
      cap >= w * h)
  {
    memcpy(out, ayther_rc_cache_pixels, w * h * 2);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return 1;
  }

  /* ---- salvar todo lo que el render muta ---- */
  s_status = status;
  s_sprovr = spr_ovr;
  s_sprcol = spr_col;
  memcpy(s_objcount, object_count, sizeof(object_count));
  memcpy(s_objinfo, obj_info, sizeof(obj_info));
  memcpy(s_clip, clip, sizeof(clip));
  s_reg11 = reg[11]; s_reg12 = reg[12]; s_reg18 = reg[18];
  s_hsmask = hscroll_mask;
  s_bdata  = bitmap.data;
  s_bpitch = bitmap.pitch;
  s_bvx = bitmap.viewport.x;
  s_bvy = bitmap.viewport.y;

  /* tiles sucios pendientes → cache (idéntico al arranque de render_line;
     el próximo frame real haría exactamente este flush) */
  if (bg_list_index)
  {
    update_bg_pattern_cache(bg_list_index);
    bg_list_index = 0;
  }

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
  s_lmask = ayther_layer_mask;
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
  if (sh_rebuilt)
  {
    int i;
    reg[12] = s_reg12;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
  }
  reg[11] = s_reg11;
  reg[12] = s_reg12;
  reg[18] = s_reg18;
  hscroll_mask = s_hsmask;
  memcpy(clip, s_clip, sizeof(clip));
  ayther_rc_nolimit = 0;
  ayther_rc_nomask  = 0;
  ayther_layer_mask = s_lmask;
  bitmap.data  = s_bdata;
  bitmap.pitch = s_bpitch;
  bitmap.viewport.x = s_bvx;
  bitmap.viewport.y = s_bvy;
  memcpy(obj_info, s_objinfo, sizeof(obj_info));
  memcpy(object_count, s_objcount, sizeof(object_count));
  status  = s_status;
  spr_ovr = s_sprovr;
  spr_col = s_sprcol;

  if (out_w) *out_w = w;
  if (out_h) *out_h = h;

  if (w * h <= 320 * 300)
  {
    ayther_rc_cache_generation = ayther_core_frame_generation;
    ayther_rc_cache_flags = flags;
    ayther_rc_cache_mask = ayther_layer_mask;
    memcpy(ayther_rc_cache_pixels, out, w * h * 2);
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
  uint16 s_status, s_sprcol;
  uint8  s_sprovr, s_lmask;
  uint8  s_objcount[2];
  object_info_t s_objinfo[2][80];
  struct clip_t s_clip[2];
  uint8  s_reg11, s_reg12, s_reg18, s_hsmask;
  uint8 *s_bdata;
  int    s_bpitch, s_bvx, s_bvy;
  int    l, w, h, vs, ste, sh_rebuilt;
  void (*rbg)(int);
  void (*robj)(int);
  
  uint16 s_cram[64];
  uint8 s_regs[0x20];
  uint8 s_vsram[0x80];
  uint8 s_hscroll_table[1024];

  w = bitmap.viewport.w;
  h = bitmap.viewport.h;

  if (!(reg[1] & 0x04)) return AYTHER_RC_ERR_NOT_MODE5;
  if ((reg[12] & 0x06) == 0x06) return AYTHER_RC_ERR_INTERLACE2;
  if (interlaced && config.render) return AYTHER_RC_ERR_INTERLACE2;
  if (config.ntsc) return AYTHER_RC_ERR_NTSC_FILTER;
  if (w <= 0 || h <= 0 || cap < w * h) return AYTHER_RC_ERR_INVALID_PARAMS;

  s_status = status;
  s_sprovr = spr_ovr;
  s_sprcol = spr_col;
  memcpy(s_objcount, object_count, sizeof(object_count));
  memcpy(s_objinfo, obj_info, sizeof(obj_info));
  memcpy(s_clip, clip, sizeof(clip));
  s_reg11 = reg[11]; s_reg12 = reg[12]; s_reg18 = reg[18];
  s_hsmask = hscroll_mask;
  s_bdata  = bitmap.data;
  s_bpitch = bitmap.pitch;
  s_bvx = bitmap.viewport.x;
  s_bvy = bitmap.viewport.y;

  if (bg_list_index)
  {
    update_bg_pattern_cache(bg_list_index);
    bg_list_index = 0;
  }

  if (flags & AYTHER_RC_FLAT_HS) hscroll_mask = 0x00;
  if (flags & AYTHER_RC_NO_WINDOW)
  {
    window_clip(0, reg[12] & 1);
    reg[18] = 0;
  }
  ayther_rc_nolimit = (flags & AYTHER_RC_NO_SPR_LIMIT) ? 1 : 0;
  ayther_rc_nomask  = (flags & AYTHER_RC_NO_SPR_MASK)  ? 1 : 0;

  s_lmask = ayther_layer_mask;

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
  memcpy(s_hscroll_table, &vram[hscb], 1024);

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
            *p = ev->data;
            int index = (ev->address >> 1) & 0x3F;
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
            reg[ev->address] = (uint8)ev->data;
            if (ev->address == 13) hscb = (reg[13] & 0x3F) << 10;
          }
          else if (ev->reason == AYTHER_RASTER_REASON_HSCROLL)
          {
            vram[ev->address] = (uint8)ev->data;
          }
          else
          {
            ayther_raster_dirty |= ev->reason;
          }
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
      memcpy(cram, s_cram, sizeof(cram));
      memcpy(reg, s_regs, sizeof(reg));
      memcpy(vsram, s_vsram, sizeof(vsram));
      memcpy(&vram[hscb], s_hscroll_table, 1024);
      hscb = (reg[13] & 0x3F) << 10;
    }
    reg[12] = s_reg12;
    color_update_m5(0x00, *(uint16 *)&cram[(reg[7] & 0x3F) << 1]);
    for (i = 1; i < 0x40; i++)
      color_update_m5(i, *(uint16 *)&cram[i << 1]);
  }
  reg[11] = s_reg11;
  reg[12] = s_reg12;
  reg[18] = s_reg18;
  hscroll_mask = s_hsmask;
  memcpy(clip, s_clip, sizeof(clip));
  ayther_rc_nolimit = 0;
  ayther_rc_nomask  = 0;
  ayther_layer_mask = s_lmask;
  bitmap.data  = s_bdata;
  bitmap.pitch = s_bpitch;
  bitmap.viewport.x = s_bvx;
  bitmap.viewport.y = s_bvy;
  memcpy(obj_info, s_objinfo, sizeof(obj_info));
  memcpy(object_count, s_objcount, sizeof(object_count));
  status  = s_status;
  spr_ovr = s_sprovr;
  spr_col = s_sprcol;

  if (out_w) *out_w = w;
  if (out_h) *out_h = h;
  return 1;
#endif
}

#endif /* AYTHER_EXTENSIONS */
