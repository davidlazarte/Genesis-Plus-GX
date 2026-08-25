/* Convertir un `__fastfail` de UCRT en una respuesta. (#45.A)
 *
 * El sintoma que #38 dejo acotado es STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
 * en el build UCRT. Ese codigo NO es agotamiento de pila -- eso es
 * STATUS_STACK_OVERFLOW (0xC00000FD)-: es lo que produce `__fastfail`, y en
 * UCRT `abort()` esta implementado sobre el, igual que el *invalid parameter
 * handler*, que MSVCRT directamente no tiene. De ahi la conclusion de trabajo:
 * el build UCRT probablemente llama a `abort()` o a ese handler, y MSVCRT no
 * "funciona" -- simplemente no reacciona igual ante el mismo problema-.
 *
 * El problema para cerrarlo es que `__fastfail` termina el proceso SIN pasar por
 * el filtro de excepciones no manejadas: emite `int 0x29` y el kernel mata. Sin
 * un depurador enganchado no queda ni un stack ni un mensaje, que es por lo que
 * el issue pedia "capturar con un depurador".
 *
 * Este header hace innecesario el depurador, enganchandose ANTES del fastfail:
 *
 *   - `_set_invalid_parameter_handler` corre en lugar del handler por defecto,
 *     y recibe expresion, funcion, archivo y linea. Eso NOMBRA la llamada a la
 *     CRT que fallo, que es exactamente el dato que falta.
 *   - un handler de SIGABRT atrapa el `abort()` explicito.
 *   - `SetUnhandledExceptionFilter` queda igual como red por si el fallo llega
 *     por otro camino.
 *
 * Se compila solo con -DAYTHER_UCRT_DIAG: es diagnostico, no algo que tenga que
 * vivir en el harness de todos los dias.
 *
 * Y ese flag solo sirve con el toolchain UCRT, cosa que el propio linker
 * confirma: contra msvcrt, `_set_abort_behavior` y `_set_invalid_parameter_handler`
 * quedan como simbolos indefinidos. Los headers de mingw-w64 los declaran para
 * las dos CRT, pero msvcrt.dll no los exporta -- no existen ahi-. Eso ES la
 * premisa del issue medida por otro lado: la superficie de validacion que UCRT
 * tiene y msvcrt no es justamente la que puede estar disparando el fastfail.
 */

#ifndef AYTHER_UCRT_DIAG_H
#define AYTHER_UCRT_DIAG_H

#if defined(AYTHER_UCRT_DIAG) && defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <wchar.h>
#include <windows.h>

static void ayther_ucrt_abort_handler(int sig)
{
  (void)sig;
  fprintf(stderr, "\n== UCRT: abort() ==\n");
  fprintf(stderr, "El proceso llamo a abort(). En UCRT eso termina en __fastfail\n"
                  "y da STATUS_STACK_BUFFER_OVERRUN (0xC0000409), que es el codigo\n"
                  "que #38 vio y que NO es agotamiento de pila.\n");
  fflush(stderr);
  _exit(3);
}

static void __cdecl ayther_ucrt_invalid_parameter(
    const wchar_t *expression, const wchar_t *function,
    const wchar_t *file, unsigned int line, uintptr_t reserved)
{
  (void)reserved;
  fprintf(stderr, "\n== UCRT: parametro invalido ==\n");
  fwprintf(stderr, L"  funcion:   %ls\n", function ? function : L"(sin nombre)");
  fwprintf(stderr, L"  expresion: %ls\n", expression ? expression : L"(sin expresion)");
  fwprintf(stderr, L"  archivo:   %ls:%u\n", file ? file : L"(sin archivo)", line);
  fprintf(stderr,
          "\nEsta es la diferencia con MSVCRT: alli este handler no existe y la\n"
          "misma llamada sigue de largo. No es que MSVCRT funcione; es que no\n"
          "valida.\n");
  fflush(stderr);
  _exit(4);
}

static LONG WINAPI ayther_ucrt_seh(EXCEPTION_POINTERS *info)
{
  const EXCEPTION_RECORD *r = info ? info->ExceptionRecord : NULL;
  HMODULE mod = NULL;
  char name[MAX_PATH] = "(desconocido)";

  fprintf(stderr, "\n== excepcion no manejada ==\n");
  if (r)
  {
    fprintf(stderr, "  codigo:      0x%08lX%s\n",
            (unsigned long)r->ExceptionCode,
            (r->ExceptionCode == 0xC0000409u) ? "  (__fastfail)" : "");
    fprintf(stderr, "  direccion:   %p\n", r->ExceptionAddress);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)r->ExceptionAddress, &mod) && mod)
    {
      GetModuleFileNameA(mod, name, sizeof(name));
      fprintf(stderr, "  modulo:      %s\n", name);
    }
  }
  fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}

static void ayther_ucrt_diag_install(void)
{
  signal(SIGABRT, ayther_ucrt_abort_handler);
  /* Que el dialogo de abort no se coma el mensaje en un runner sin consola. */
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_invalid_parameter_handler(ayther_ucrt_invalid_parameter);
  SetUnhandledExceptionFilter(ayther_ucrt_seh);
  fprintf(stderr, "UCRT diag: handlers instalados\n");
  fflush(stderr);
}

#else
#define ayther_ucrt_diag_install() ((void)0)
#endif

#endif /* AYTHER_UCRT_DIAG_H */
