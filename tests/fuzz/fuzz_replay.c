/* Driver para correr un target de fuzzing SIN libFuzzer. (#34)
 *
 * libFuzzer necesita clang. Este driver necesita un compilador de C, y con eso
 * alcanza para lo que hace falta a diario:
 *
 *   - correr el corpus y `regressions/` en `make check`, que es lo que evita
 *     que un crash arreglado vuelva sin que nadie se entere;
 *   - compilar y ejercitar los targets en una maquina sin clang, para que
 *     "el target sigue funcionando" no sea una afirmacion que solo el job
 *     nocturno puede verificar.
 *
 * Cada archivo que se le pasa es UNA entrada, tal cual la vera libFuzzer.
 *
 * Uso: fuzz_replay [--scene <nombre>] <core> <archivo-o-directorio>...
 *
 * La escena (#75) elige que consola y que core de FM levanta el harness.
 * Va por argumento y no por entorno porque las recetas de este repo tienen
 * que valer en sh y en cmd, y `VAR=x cmd` solo vale en el primero; el driver
 * la traduce a AYTHER_FUZZ_SCENE, que es lo que lee fuzz_common.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#endif

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

#define FUZZ_MAX_INPUT (4u * 1024u * 1024u)

/* Watchdog por entrada. (#138)
 *
 * Un crash pone el replay en rojo. Un CUELGUE no ponia nada: el proceso se
 * quedaba en el bucle y el job de PR con el, hasta el limite de seis horas de
 * Actions, sin decir que entrada fue. El primer caso fue un `freqInc` del PSG
 * en cero, que deja a psg_update en `while (timestamp < clocks) timestamp += 0`;
 * libFuzzer lo vio porque trae su propio `-timeout`, y este driver, que es el
 * que corre en cada PR, no traia ninguno.
 *
 * Una entrada son un unserialize y tres frames (o un frame, en los otros
 * targets), milisegundos incluso con ASan; 60 s es dos ordenes de magnitud
 * de margen y AYTHER_FUZZ_INPUT_SECONDS lo cambia sin recompilar. Sale con 70,
 * el mismo codigo que usa libFuzzer para un timeout, y con el nombre del
 * archivo: es lo que hay que copiar a regressions/ cuando se arregle.
 */
#define FUZZ_INPUT_SECONDS_DEFAULT 60u

static const char *watchdog_path;
static volatile unsigned watchdog_serial;   /* cambia con cada entrada */

static unsigned watchdog_seconds(void)
{
  const char *v = getenv("AYTHER_FUZZ_INPUT_SECONDS");
  unsigned n = (v && *v) ? (unsigned)strtoul(v, NULL, 10) : FUZZ_INPUT_SECONDS_DEFAULT;
  return n ? n : FUZZ_INPUT_SECONDS_DEFAULT;
}

static void watchdog_fire(void)
{
  /* Solo llamadas seguras desde una senal: nada de printf. */
  static const char pre[] = "\n  TIMEOUT ";
  static const char post[] = ": la entrada no termino a tiempo\n";
  const char *p = watchdog_path ? watchdog_path : "?";
#if defined(_WIN32)
  DWORD w;
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  WriteFile(err, pre, (DWORD)(sizeof(pre) - 1), &w, NULL);
  WriteFile(err, p, (DWORD)strlen(p), &w, NULL);
  WriteFile(err, post, (DWORD)(sizeof(post) - 1), &w, NULL);
  _exit(70);
#else
  ssize_t r;
  r = write(2, pre, sizeof(pre) - 1);
  r = write(2, p, strlen(p));
  r = write(2, post, sizeof(post) - 1);
  (void)r;
  _exit(70);
#endif
}

#if defined(_WIN32)
/* Sin alarm() en Windows: un hilo mira cada segundo si la entrada sigue
   siendo la misma. Es el equivalente exacto, sin precision de un segundo. */
static DWORD WINAPI watchdog_thread(LPVOID arg)
{
  unsigned limit = watchdog_seconds();
  unsigned seen = watchdog_serial, elapsed = 0;
  (void)arg;
  for (;;)
  {
    Sleep(1000);
    if (watchdog_serial != seen) { seen = watchdog_serial; elapsed = 0; continue; }
    if (++elapsed >= limit) watchdog_fire();
  }
  return 0;
}
static void watchdog_start(void)
{
  HANDLE h = CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
  if (h) CloseHandle(h);
}
static void watchdog_arm(const char *path)   { watchdog_path = path; ++watchdog_serial; }
static void watchdog_disarm(void)            { watchdog_path = NULL; ++watchdog_serial; }
#else
static void watchdog_signal(int sig) { (void)sig; watchdog_fire(); }
static void watchdog_start(void)  { signal(SIGALRM, watchdog_signal); }
static void watchdog_arm(const char *path)
{ watchdog_path = path; ++watchdog_serial; alarm(watchdog_seconds()); }
static void watchdog_disarm(void) { alarm(0); watchdog_path = NULL; ++watchdog_serial; }
#endif

static int run_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  size_t n;

  if (!f) { fprintf(stderr, "no se puede abrir %s\n", path); return 0; }
  buf = (unsigned char *)malloc(FUZZ_MAX_INPUT);
  if (!buf) { fclose(f); fprintf(stderr, "sin memoria\n"); return 0; }
  n = fread(buf, 1, FUZZ_MAX_INPUT, f);
  fclose(f);

  watchdog_arm(path);
  LLVMFuzzerTestOneInput(buf, n);
  watchdog_disarm();
  free(buf);
  printf("  ok  %-52s %lu bytes\n", path, (unsigned long)n);
  return 1;
}

static int run_dir(const char *dir)
{
  int count = 0;
  char path[1024];
#if defined(_WIN32)
  WIN32_FIND_DATAA fd;
  HANDLE h;
  snprintf(path, sizeof(path), "%s\\*", dir);
  h = FindFirstFileA(path, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (fd.cFileName[0] == '.') continue;   /* .gitkeep y compania */
    snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
    count += run_file(path);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  struct dirent *e;
  if (!d) return 0;
  while ((e = readdir(d)) != NULL) {
    struct stat st;
    if (e->d_name[0] == '.') continue;
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    count += run_file(path);
  }
  closedir(d);
#endif
  return count;
}

static int is_dir(const char *p)
{
#if defined(_WIN32)
  DWORD a = GetFileAttributesA(p);
  return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* Poner una variable de entorno, que es como viajan la ruta del core y la
   escena: libFuzzer es dueno de argv y no deja pasar argumentos propios,
   asi que los dos caminos -- fuzzer y replay-- leen lo mismo. */
static void fuzz_setenv(const char *key, const char *value)
{
#if defined(_WIN32)
  char var[1200];
  snprintf(var, sizeof(var), "%s=%s", key, value);
  _putenv(var);
#else
  setenv(key, value, 1);
#endif
}

int main(int argc, char **argv)
{
  int i, total = 0;

  /* --scene <nombre> y --workdir <dir>, opcionales y siempre primero. El
     segundo (#97) es donde la escena de CD deja su BIOS y su imagen. */
  while (argc > 2) {
    if (!strcmp(argv[1], "--scene"))        fuzz_setenv("AYTHER_FUZZ_SCENE", argv[2]);
    else if (!strcmp(argv[1], "--workdir")) fuzz_setenv("AYTHER_FUZZ_WORKDIR", argv[2]);
    else break;
    argv += 2;
    argc -= 2;
  }

  if (argc < 3) {
    fprintf(stderr,
            "uso: %s [--scene <nombre>] [--workdir <dir>] <core> <archivo-o-directorio>...\n",
            argv[0]);
    return 2;
  }

  /* El target lo lee de aca: libFuzzer es dueno de argv y no deja pasar
     argumentos propios, asi que la ruta del core viaja por el entorno en los
     dos caminos. Uno solo de verdad, no dos que se parecen. */
  fuzz_setenv("AYTHER_FUZZ_CORE", argv[1]);

  watchdog_start();
  for (i = 2; i < argc; ++i)
    total += is_dir(argv[i]) ? run_dir(argv[i]) : run_file(argv[i]);

  if (!total) {
    fprintf(stderr, "no se ejecuto ninguna entrada: el corpus esta vacio?\n");
    return 1;
  }
  printf("%d entradas reproducidas sin crash\n", total);
  return 0;
}
