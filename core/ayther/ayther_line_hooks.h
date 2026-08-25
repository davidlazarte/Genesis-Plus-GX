/* Lo que `render_line` hace por el fork, en un solo punto. (#43 punto 4)
 *
 * Estos dos bloques estaban ENTRELAZADOS adentro de `render_line`, en el medio
 * del cuerpo de upstream: dos `#ifdef AYTHER_EXTENSIONS` de 58 y 22 lineas
 * separados por una linea en blanco, los dos justo antes de `render_bg(line)`.
 * Leer render_line en un rebase significaba leer eso tambien y decidir, linea
 * por linea, que era de upstream y que nuestro.
 *
 * Ahora es una llamada. El cuerpo no cambio -- se mudo tal cual-, y como es
 * `AYTHER_HOT_INLINE` con `ayther_observed` constante en cada clon, el clon
 * rapido lo borra entero igual que antes: no queda ni la llamada.
 *
 * Requiere ayther_render_state.h incluido antes.
 */

#ifndef AYTHER_LINE_HOOKS_H
#define AYTHER_LINE_HOOKS_H

#ifdef AYTHER_EXTENSIONS

/* Se llama una vez por linea, justo antes de render_bg(). */
AYTHER_HOT_INLINE void ayther_line_before_bg(int line, int ayther_observed,
                                             int ayther_attrib_capture_pending)
{
      /* AYTHER: ocultar tile por celda (id 0x104) → "pela una capa" en el merge de
         render_bg, revelando el plano de atrás (ver ayther_peel_merge). Se activa
         sólo si esta línea tiene alguna celda marcada (descarte rápido: 8 bytes de
         la fila), en coords del frame que ve el frontend (+ viewport.x/y; 0 con
         overscan off, el caso normal de MD). Se apaga antes de los sprites para no
         pelar sus merges (un sprite sobre un tile oculto sigue visible). */
      ayther_peel_active = 0;
      if (ayther_observed)
      {
        /* AYTHER (#28): la fila de celda va en coordenadas del frame EMITIDO, y
           con salida entrelazada `remap_line` duplica la fila de salida
           (`line * 2 + odd_frame`). Sin replicar ese ajuste aca, la mascara caia
           en la mitad de la fila que el frontend habia marcado: ocultaba la celda
           equivocada, que es peor que no ocultar nada. En interlace mode 2 sin
           `config.render` la salida NO se dobla y `line` ya es la fila correcta. */
        int emitted = line + bitmap.viewport.y;
        if (interlaced && config.render)
          emitted = (emitted * 2) + odd_frame;
        {
        const int frow = emitted >> 3;
        if (frow >= 0 && frow < AYTHER_TILE_ROWS)
        {
          const uint8 *rb = &ayther_tile_suppress[(frow * AYTHER_TILE_COLS) >> 3];
          if (rb[0]|rb[1]|rb[2]|rb[3]|rb[4]|rb[5]|rb[6]|rb[7])
          {
            ayther_peel_active = 1;
            ayther_peel_row    = frow;
            ayther_peel_vx     = bitmap.viewport.x;
          }
        }
        }
      }

      /* AYTHER (#41): capturar la atribución de fondo dentro de este render_bg.
         El flag envuelve SÓLO esta llamada: la recomposición usa los mismos
         renderers y, si quedara encendido, una lectura pisaría la atribución del
         frame — el resultado dependería de si alguien miró. */
      /* Los stores globales van DENTRO del guard: en el perfil compilado-idle
         `pending` es 0 y el objetivo es que no quede ni una escritura de mas por
         linea. Escribir siempre costaba dos stores a globales por linea, y ademas
         le dice al compilador que esos globales pueden cambiar, lo cual le impide
         mantener cosas en registros dentro del renderer. */
      if (ayther_attrib_capture_pending)
      {
        ayther_attrib_capture = 1;
        ayther_attrib_row = line;
        /* Las dimensiones son las del frame emitido y se refrescan por línea: el
           viewport puede cambiar entre frames y el consumidor tiene que poder
           interpretar el buffer sin adivinarlas. */
        ayther_attrib_width  = (uint32)bitmap.viewport.w;
        ayther_attrib_height = (uint32)bitmap.viewport.h;
        ayther_attrib_flags  = (interlaced && config.render) ? 1u : 0u;
      }
}

/* La pasada de sprites de `render_line`, con lo que el fork necesita alrededor.
 *
 * Reemplaza UNA linea de upstream -- `render_obj(line & 1)`-- por 76, y eso
 * vivia en el medio de render_line. El cuerpo es el mismo; lo que cambia es que
 * el diff con upstream vuelve a mostrar una llamada donde upstream tiene una
 * llamada. */
AYTHER_HOT_INLINE void ayther_line_render_obj(int line, int ayther_observed,
                                              int ayther_attrib_capture_pending,
                                              int ayther_show_obj,
                                              int ayther_dim_active)
{
      /* Condicional por lo mismo: si nunca se encendio, no hace falta apagarlo. */
      if (ayther_attrib_capture_pending)
        ayther_attrib_capture = 0;
      /* AYTHER: el peel sólo aplica a los merges de BG, no a los de sprites. */
      ayther_peel_active = 0;

      /* Solo para el fallback de Mode 4; en Mode 5 el bit de sprite es exacto y
         este snapshot no se usa. */
      if ((ayther_dim_active || ayther_attrib_capture_pending) && !(reg[1] & 0x04))
        memcpy(ayther_bg_snap, linebuf[0], sizeof(ayther_bg_snap));

      /* Render sprite layer (AYTHER: ocultable vía máscara de capas, id 0x102).
         AYTHER dim (id 0x108): snapshot de linebuf[0] tras render_bg + diff tras
         render_obj → los píxeles que cambió render_obj son sprites (los demás son
         fondo, que remap_line atenúa). No toca las internas de render_obj. */
      if (ayther_dim_active || ayther_attrib_capture_pending)
      {
        /* #31/#37/#41: el bit de sprite lo escribe QUIEN DECIDE la prioridad --
           el bucle de render_obj en la familia sin S/H, el merge en la familia
           con S/H-. Antes salia de comparar linebuf[0] antes y despues, y esa
           via perdia los pixeles de sprite iguales al fondo y marcaba como
           sprite los operadores de brillo. */
        ayther_obj_px_exact = 0;
        if (ayther_show_obj)
        {
          ayther_obj_pass = 1;
          render_obj(line & 1);
          ayther_obj_pass = 0;
        }
        if (!ayther_obj_px_exact)
        {
          /* Mode 4 y TMS no pasan por ninguno de los dos: ahi sigue el diff, con
             su defecto conocido, hasta que #40 fase 2 meta esos modos en alcance.
             El snapshot se toma SOLO en esos modos, asi que fuera de ellos la
             respuesta correcta es "ningun pixel es de sprite" y no un diff contra
             un buffer que nadie lleno. */
          int i;
          if (ayther_show_obj && !(reg[1] & 0x04))
            for (i = 0; i < 0x200; i++)
              ayther_sprite_px[i] = (linebuf[0][i] != ayther_bg_snap[i]);
          else
            memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
        }
      }
      else if (ayther_show_obj)
        render_obj(line & 1);

      /* AYTHER (#41): volcar la fila al buffer del frame.
         El bit de sprite ya NO sale de un diff contra el fondo: lo escribe quien
         decide la prioridad. La limitación conocida —un píxel de sprite cuyo byte
         coincide con el del fondo quedaba sin marcar— queda cerrada, y de paso
         dejan de marcarse como sprite los operadores de shadow/highlight, que no
         lo son (#31 defecto 2). */
      if (ayther_attrib_capture_pending)
      {
        const uint32 w = (uint32)bitmap.viewport.w;
        const int row = ayther_attrib_row + bitmap.viewport.y;
        if (row >= 0 && (uint32)row < ayther_attrib_height && w <= 320)
        {
          uint8 *out = &ayther_attrib[(size_t)row * ayther_attrib_width];
          uint32 x;
          for (x = 0; x < w && x < ayther_attrib_width; ++x)
          {
            uint8 attr = ayther_attrib_line[x];
            if (ayther_sprite_px[0x20 + x])
              attr |= AYTHER_ATTRIB_SPRITE;
            out[x] = attr;
          }
        }
      }
}

#else
#define ayther_line_before_bg(line, observed, pending) \
  ((void)(line), (void)(observed), (void)(pending))
#define ayther_line_render_obj(line, observed, pending, show, dim) \
  ((void)(line), (void)(observed), (void)(pending), (void)(show), (void)(dim))
#endif /* AYTHER_EXTENSIONS */

#endif /* AYTHER_LINE_HOOKS_H */
