/* #97: los archivos del fixture de Sega CD, escritos donde el core los busca.
 *
 * A diferencia de un cartucho, un Sega CD no entra por `retro_game_info.data`:
 * `cdd_load` abre la imagen por RUTA (cdStreamOpen) y `load_bios` abre la BIOS
 * por ruta desde el directorio de sistema del frontend. Los dos son archivos
 * que el core lee del disco, asi que el fixture tiene que existir en el disco
 * antes de `retro_load_game`, y el harness tiene que contestar
 * RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY con el directorio donde los dejo.
 *
 * La BIOS se escribe con los TRES nombres de region: la region la decide el
 * core a partir de la cabecera de la imagen y de config.region_detect, y un
 * fixture que solo funcionara con la deteccion actual se romperia en silencio
 * el dia que alguien cambie el default.
 *
 * Es un header con funciones static a proposito: lo incluyen dos binarios
 * distintos (state_roundtrip y los targets de fuzz) que no comparten mas que
 * generated_rom.c, y meter stdio en generated_rom.c lo convertiria en algo que
 * ya no es un generador puro de bytes.
 */
#ifndef AYTHER_CD_FIXTURE_H
#define AYTHER_CD_FIXTURE_H

#include <stdio.h>
#include <string.h>
#include "generated_rom.h"

static int ayther_cd_write_file(const char *path, const uint8_t *data, size_t n)
{
  FILE *f = fopen(path, "wb");
  size_t w;
  if (!f) return 0;
  w = fwrite(data, 1, n, f);
  return fclose(f) == 0 && w == n;
}

/* Escribe la BIOS (x3 regiones) y la imagen en `dir`, y deja en `iso_path` la
   ruta de la imagen, que es lo que se le pasa a retro_load_game. Devuelve 0 si
   algo no se pudo generar o escribir. `flags` elige la variante de BIOS
   (AYTHER_CD_BIOS_*); 0 es la de siempre. */
static int ayther_cd_fixture_write_ex(const char *dir, char *iso_path, size_t cap,
                                      unsigned int flags)
{
  static uint8_t bios[AYTHER_CD_BIOS_SIZE];
  static uint8_t iso[AYTHER_CD_ISO_SIZE];
  static const char *const bios_names[3] =
    { "bios_CD_U.bin", "bios_CD_E.bin", "bios_CD_J.bin" };
  char path[1024];
  int i;

  if (!ayther_build_generated_cd_bios_ex(bios, sizeof(bios), flags)) return 0;
  if (!ayther_build_generated_cd_image(iso, sizeof(iso))) return 0;

  for (i = 0; i < 3; ++i) {
    snprintf(path, sizeof(path), "%s/%s", dir, bios_names[i]);
    if (!ayther_cd_write_file(path, bios, sizeof(bios))) return 0;
  }
  snprintf(iso_path, cap, "%s/%s", dir, AYTHER_CD_ISO_NAME);
  return ayther_cd_write_file(iso_path, iso, sizeof(iso));
}

static int ayther_cd_fixture_write(const char *dir, char *iso_path, size_t cap)
{
  return ayther_cd_fixture_write_ex(dir, iso_path, cap, 0u);
}

#endif /* AYTHER_CD_FIXTURE_H */
