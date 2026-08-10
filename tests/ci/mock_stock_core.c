/* Minimal shared library with no AYTHER symbol for safe-discovery testing. */

#if defined(_WIN32)
#define MOCK_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define MOCK_EXPORT __attribute__((visibility("default")))
#else
#define MOCK_EXPORT
#endif

MOCK_EXPORT unsigned retro_api_version(void)
{
  return 1;
}
