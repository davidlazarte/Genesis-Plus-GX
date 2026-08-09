/* Load a shared library and verify required exports without calling them. */

#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE library_t;

static library_t open_library(const char *path)
{
  return LoadLibraryA(path);
}

static int has_symbol(library_t library, const char *name)
{
  return GetProcAddress(library, name) != NULL;
}

static void close_library(library_t library)
{
  FreeLibrary(library);
}

static void print_load_error(const char *path)
{
  fprintf(stderr, "cannot load %s (Windows error %lu)\n",
          path, (unsigned long)GetLastError());
}
#else
#include <dlfcn.h>
typedef void *library_t;

static library_t open_library(const char *path)
{
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static int has_symbol(library_t library, const char *name)
{
  return dlsym(library, name) != NULL;
}

static void close_library(library_t library)
{
  dlclose(library);
}

static void print_load_error(const char *path)
{
  const char *error = dlerror();
  fprintf(stderr, "cannot load %s: %s\n", path, error ? error : "unknown error");
}
#endif

int main(int argc, char **argv)
{
  library_t library;
  int i;
  int missing = 0;

  if (argc < 3)
  {
    fprintf(stderr, "usage: %s LIBRARY REQUIRED_EXPORT [REQUIRED_EXPORT...]\n",
            argv[0]);
    return 2;
  }

  library = open_library(argv[1]);
  if (!library)
  {
    print_load_error(argv[1]);
    return 1;
  }

  for (i = 2; i < argc; ++i)
  {
    if (!has_symbol(library, argv[i]))
    {
      fprintf(stderr, "missing dynamic export: %s\n", argv[i]);
      missing = 1;
    }
  }

  close_library(library);
  if (missing)
    return 1;

  printf("loaded %s and resolved %d required exports\n", argv[1], argc - 2);
  return 0;
}
