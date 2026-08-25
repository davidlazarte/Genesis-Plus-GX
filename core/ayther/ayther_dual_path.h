/* El patron fast/observed del fork, en un solo lugar. (#43 punto 3)
 *
 * La idea. Una funcion caliente del core -- el render de una linea, el update
 * del PSG-- tiene que poder OBSERVARSE cuando alguien se suscribio, y no pagar
 * un centavo cuando nadie lo hizo. La forma que el fork usa es un cuerpo unico
 * parametrizado por un flag de compilacion (`ayther_observed`), dos clones que
 * lo instancian con 0 y con 1, y un despachador que elige. Como el flag es
 * constante dentro de cada clon, el compilador borra la rama entera del clon
 * rapido: no queda ni el `if`.
 *
 * Por que un macro. Ese triplete estaba escrito A MANO SIETE VECES -- cinco en
 * vdp_render.c, uno en psg.c, uno en ym2612.c-, con las mismas doce lineas y
 * los mismos dos atributos en cada copia. Nada obligaba a que las siete
 * coincidieran, y la que se desviara no fallaria: `noinline` olvidado en un
 * clon significa que el compilador puede fusionarlo con el otro y meter de
 * vuelta el `if` en el camino caliente. Un error de rendimiento silencioso, del
 * tipo que solo aparece en un profile que nadie corre.
 *
 * `AYTHER_NOINLINE` es obligatorio en los clones y no una sugerencia: sin el,
 * la separacion que da todo el sentido al patron se deshace.
 *
 * Los atributos, ademas, estaban definidos en TRES archivos con tres nombres
 * distintos (`AYTHER_NOINLINE`, `AYTHER_PSG_NOINLINE`, `AYTHER_YM_NOINLINE`)
 * para exactamente lo mismo. Aca hay uno.
 */

#ifndef AYTHER_DUAL_PATH_H
#define AYTHER_DUAL_PATH_H

/* Cuerpo compartido: se expande en los dos clones, asi que tiene que poder
   inlinearse siempre. Sin `always_inline` el compilador puede decidir que 500
   lineas no valen la pena y dejar una llamada con el flag como parametro
   RUNTIME, que es justo lo que este patron evita. */
#if defined(AYTHER_EXTENSIONS) && defined(__GNUC__)
#define AYTHER_HOT_INLINE static inline __attribute__((always_inline))
#else
#define AYTHER_HOT_INLINE INLINE
#endif

/* Clones: lo contrario. Tienen que quedar separados. */
#if defined(__GNUC__) || defined(__clang__)
#define AYTHER_NOINLINE __attribute__((noinline))
#else
#define AYTHER_NOINLINE
#endif

#ifdef AYTHER_EXTENSIONS

/* AYTHER_DUAL_PATH(pub, base, predicate, params, args...)
 *
 *   pub       nombre publico que ven los demas archivos
 *   base      prefijo del cuerpo; se llama `base##_impl`
 *   predicate expresion que decide, evaluada UNA vez por llamada
 *   params    lista de parametros con parentesis, p.ej. (int line)
 *   ...       los mismos parametros por nombre, para reenviarlos
 *
 * `pub` y `base` se pasan por separado porque no siempre coinciden:
 * `YM2612Update` despacha sobre `ym2612_update_impl`.
 */
#define AYTHER_DUAL_PATH(pub, base, predicate, params, ...)                   \
  static AYTHER_NOINLINE void base##_fast_path params                         \
  {                                                                           \
    base##_impl(__VA_ARGS__, 0);                                              \
  }                                                                           \
  static AYTHER_NOINLINE void base##_observed_path params                     \
  {                                                                           \
    base##_impl(__VA_ARGS__, 1);                                              \
  }                                                                           \
  void pub params                                                             \
  {                                                                           \
    if (predicate)                                                            \
      base##_observed_path(__VA_ARGS__);                                      \
    else                                                                      \
      base##_fast_path(__VA_ARGS__);                                          \
  }

/* Igual, para un despachador que no sale del archivo. */
#define AYTHER_DUAL_PATH_STATIC(pub, base, predicate, params, ...)            \
  static AYTHER_NOINLINE void base##_fast_path params                         \
  {                                                                           \
    base##_impl(__VA_ARGS__, 0);                                              \
  }                                                                           \
  static AYTHER_NOINLINE void base##_observed_path params                     \
  {                                                                           \
    base##_impl(__VA_ARGS__, 1);                                              \
  }                                                                           \
  static void pub params                                                      \
  {                                                                           \
    if (predicate)                                                            \
      base##_observed_path(__VA_ARGS__);                                      \
    else                                                                      \
      base##_fast_path(__VA_ARGS__);                                          \
  }

#else /* sin extensiones no hay nada que observar: queda una sola funcion */

#define AYTHER_DUAL_PATH(pub, base, predicate, params, ...)                   \
  void pub params                                                             \
  {                                                                           \
    base##_impl(__VA_ARGS__, 0);                                              \
  }

#define AYTHER_DUAL_PATH_STATIC(pub, base, predicate, params, ...)            \
  static void pub params                                                      \
  {                                                                           \
    base##_impl(__VA_ARGS__, 0);                                              \
  }

#endif /* AYTHER_EXTENSIONS */

#endif /* AYTHER_DUAL_PATH_H */
