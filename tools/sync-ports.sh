#!/bin/sh
set -eu

manifest="${1:-ports/ports.list}"
enabled_list="${2:-ports/ports-enabled.list}"
ports_root="ports"
makefiles_dir="${ports_root}/makefiles"
archives_dir="${ports_root}/dist"
log_file="${ports_root}/ports-sync.log"
skip_list=" dash dwm st "

mkdir -p "${ports_root}" "${makefiles_dir}" "${archives_dir}"
: > "${log_file}"

if [ ! -f "${manifest}" ]; then
  echo "ports-sync: manifest not found: ${manifest}" | tee -a "${log_file}" >&2
  exit 1
fi

if [ ! -f "${enabled_list}" ]; then
  echo "ports-sync: enabled-list not found: ${enabled_list}" | tee -a "${log_file}" >&2
  exit 1
fi

is_enabled_port() {
  port_name="$1"
  awk '
    {
      line = $0
      sub(/#.*/, "", line)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
      if (line == "") {
        next
      }
      if (line == needle) {
        found = 1
        exit
      }
    }
    END { if (!found) exit 1 }
  ' needle="${port_name}" "${enabled_list}"
}

extract_archive() {
  archive_path="$1"
  outdir="$2"

  case "${archive_path}" in
    *.zip)
      if ! command -v unzip >/dev/null 2>&1; then
        echo "ports-sync: unzip is required for ${archive_path}" | tee -a "${log_file}" >&2
        return 1
      fi
      unzip -q "${archive_path}" -d "${outdir}"
      ;;
    *)
      tar -xf "${archive_path}" -C "${outdir}"
      ;;
  esac
}

while IFS='|' read -r name url pclass srcdir binname; do
  case "${name}" in
    ""|\#*)
      continue
      ;;
  esac

  case "${skip_list}" in
    *" ${name} "*)
      echo "ports-sync: skip in-tree port ${name}" >> "${log_file}"
      continue
      ;;
  esac

  if ! is_enabled_port "${name}"; then
    echo "ports-sync: skip disabled port ${name}" >> "${log_file}"
    continue
  fi

  if [ -z "${url}" ]; then
    echo "ports-sync: missing URL for ${name}" | tee -a "${log_file}" >&2
    continue
  fi

  if [ -z "${srcdir}" ]; then
    srcdir="${name}"
  fi

  src_path="${ports_root}/${srcdir}"
  archive_name="$(basename "${url}")"
  archive_path="${archives_dir}/${archive_name}"

  if [ ! -d "${src_path}" ]; then
    echo "ports-sync: fetching ${name} from ${url}" | tee -a "${log_file}"
    if command -v curl >/dev/null 2>&1; then
      curl -L --fail -o "${archive_path}" "${url}"
    elif command -v wget >/dev/null 2>&1; then
      wget -O "${archive_path}" "${url}"
    else
      echo "ports-sync: need curl or wget to fetch ${name}" | tee -a "${log_file}" >&2
      continue
    fi

    tmp_extract="${ports_root}/.extract-${name}.$$"
    rm -rf "${tmp_extract}"
    mkdir -p "${tmp_extract}"

    if ! extract_archive "${archive_path}" "${tmp_extract}"; then
      rm -rf "${tmp_extract}"
      continue
    fi

    extracted_top="$(find "${tmp_extract}" -mindepth 1 -maxdepth 1 | head -n 1)"
    if [ -z "${extracted_top}" ]; then
      echo "ports-sync: archive for ${name} extracted no files" | tee -a "${log_file}" >&2
      rm -rf "${tmp_extract}"
      continue
    fi

    if [ -d "${extracted_top}" ]; then
      mv "${extracted_top}" "${src_path}"
    else
      mkdir -p "${src_path}"
      mv "${tmp_extract}"/* "${src_path}/"
    fi
    rm -rf "${tmp_extract}"
  else
    echo "ports-sync: source already present for ${name} at ${src_path}" >> "${log_file}"
  fi

  custom_mk="${makefiles_dir}/${name}.Makefile"
  dst_mk="${src_path}/Makefile.auxv6"
  if [ -f "${custom_mk}" ]; then
    cp "${custom_mk}" "${dst_mk}"
    echo "ports-sync: installed ${dst_mk}" >> "${log_file}"
  else
    echo "ports-sync: missing custom makefile ${custom_mk} for ${name}" | tee -a "${log_file}"
  fi
done < "${manifest}"

echo "ports-sync: complete (see ${log_file})"
