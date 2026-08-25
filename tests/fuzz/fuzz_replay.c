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
 * Uso: fuzz_replay <core> <archivo-o-directorio>...
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
#endif

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

#define FUZZ_MAX_INPUT (4u * 1024u * 1024u)

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

  LLVMFuzzerTestOneInput(buf, n);
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

int main(int argc, char **argv)
{
  int i, total = 0;

  if (argc < 3) {
    fprintf(stderr, "uso: %s <core> <archivo-o-directorio>...\n", argv[0]);
    return 2;
  }

  /* El target lo lee de aca: libFuzzer es dueno de argv y no deja pasar
     argumentos propios, asi que la ruta del core viaja por el entorno en los
     dos caminos. Uno solo de verdad, no dos que se parecen. */
#if defined(_WIN32)
  {
    char var[1200];
    snprintf(var, sizeof(var), "AYTHER_FUZZ_CORE=%s", argv[1]);
    _putenv(var);
  }
#else
  setenv("AYTHER_FUZZ_CORE", argv[1], 1);
#endif

  for (i = 2; i < argc; ++i)
    total += is_dir(argv[i]) ? run_dir(argv[i]) : run_file(argv[i]);

  if (!total) {
    fprintf(stderr, "no se ejecuto ninguna entrada: el corpus esta vacio?\n");
    return 1;
  }
  printf("%d entradas reproducidas sin crash\n", total);
  return 0;
}
