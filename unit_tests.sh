#!/usr/bin/env bash
set -euo pipefail

# Config
CFX="./cfx"                                # CLI tool under test [2]
INPUT_LABEL="${INPUT_LABEL:-Makefile}"    # Test input file path (override via env or edit here) [2]
BMP_HOME="$HOME/CFET-Tools/Makefile.bmp"      # Default output BMP path [2]
BMP_LINK="CFET/cfx.bmp"                    # Symlink in working directory [2]
PASSWORD="testpass123"                     # Used only when AIO + encryption are enabled [2]
RESULTS_CSV="cfx_test_results.csv"         # Output CSV for original mode [2]
COMPRESS_RESULTS_CSV="cfx_compression_test_results.csv" # Output CSV for compression mode [2]
DECODED_PATH="$HOME/CFET-Tools/$INPUT_LABEL"
# Compression test mode
COMPRESS_MODE=false
if [[ "${1:-}" == "compress" ]]; then
  COMPRESS_MODE=true
fi

# Dependencies check
for cmd in md5sum stat awk date; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "Missing dependency: $cmd"; exit 1; }
done

# Helper: nanoseconds timestamp (Linux, GNU coreutils)
now_ns() { date +%s%N; }  # Requires GNU date on Linux [8][6]

# Helper: pick existing BMP path
get_bmp_path() {
  if [[ -e "$BMP_LINK" ]]; then
    printf "%s" "$BMP_LINK"
  else
    printf "%s" "$BMP_HOME"
  fi
}

# Helper: run a command, optionally piping password for AIO-encrypted flows
run_cmd() {
  local needs_password="$1"; shift
  if [[ "$needs_password" == "yes" ]]; then
    printf "%s\n" "$PASSWORD" | "$@"
  else
    "$@"
  fi
}

# Validate inputs
if [[ ! -x "$CFX" ]]; then
  echo "Error: $CFX not found or not executable"
  exit 1
fi
if [[ ! -f "$INPUT_LABEL" && ! -d "$INPUT_LABEL" ]]; then
  echo "Error: input file not found: $INPUT_LABEL"
  exit 1
fi

# Record original MD5 and size of INPUT_LABEL before any overwrite
ORIG_MD5=$(md5sum "$INPUT_LABEL" | awk '{print $1}')   # MD5 integrity of input file [1]
ORIG_SIZE_BYTES=$(stat -c %s "$INPUT_LABEL")           # File size in bytes [1]

# Compression test mode
if [[ "$COMPRESS_MODE" == true ]]; then
  echo "Running compression tests with levels 1-21..."
  echo "level,flags,encode_ms,decode_ms,encode_MB_per_min,decode_MB_per_min,compression_ratio,compressed_size_bytes,md5_ok,bmp_path" > "$COMPRESS_RESULTS_CSV"

  # Test compression levels 1-21 with default flags -a -ne -gs
  for level in {1..21}; do
    echo "Testing compression level $level..."

    # Cleanup previous artifacts
    rm -f "$BMP_HOME" "$BMP_LINK" 2>/dev/null || true

    # Build flags: -a -ne -gs -c <level>
    flags=("-a" "-ne" "-gs" "-c" "$level")

    # Encode using only INPUT_LABEL as input
    start_ns=$(now_ns)
    run_cmd "no" "$CFX" -f "$INPUT_LABEL" "${flags[@]}"
    end_ns=$(now_ns)
    encode_ns=$((end_ns - start_ns))
    encode_ms=$((encode_ns / 1000000))

    # Determine BMP path
    BMP_PATH="$(get_bmp_path)"
    if [[ ! -f "$BMP_PATH" ]]; then
      echo "Level $level: BMP not found at $BMP_HOME nor $BMP_LINK"
      echo "$level,\"${flags[*]}\",$encode_ms,NA,NA,NA,NA,NA,FAIL-BMP-NOT-FOUND,$BMP_PATH" >> "$COMPRESS_RESULTS_CSV"
      continue
    fi

    # Get compressed file size for ratio calculation
    COMPRESSED_SIZE_BYTES=$(stat -c %s "$BMP_PATH")

    # Calculate compression ratio (original_size / compressed_size)
    compression_ratio=$(awk -v orig="$ORIG_SIZE_BYTES" -v comp="$COMPRESSED_SIZE_BYTES" 'BEGIN{print orig/comp}')

    # Backup the original binary, then run decode which may overwrite ./cfx
    cp -f "$CFX" "${CFX}.backup"

    # Decode from BMP; decode does not read INPUT_LABEL, but output integrity is checked against INPUT_LABEL
    start_ns=$(now_ns)
    run_cmd "no" "$CFX" -i "$BMP_PATH"
    end_ns=$(now_ns)
    decode_ns=$((end_ns - start_ns))
    decode_ms=$((decode_ns / 1000000))

    # Verify MD5 integrity: compare reconstructed file (./cfx) vs original input file digest
    NEW_MD5=$(md5sum "$DECODED_PATH" | awk '{print $1}')
    md5_ok="FAIL"
    if [[ "$NEW_MD5" == "$ORIG_MD5" ]]; then
      md5_ok="OK"
    fi

    # Throughput (MiB/min)
    encode_MB_per_min="NA"
    decode_MB_per_min="NA"
    if (( encode_ms > 0 )); then
      encode_MB_per_min=$(awk -v bytes="$ORIG_SIZE_BYTES" -v ms="$encode_ms" 'BEGIN{print (bytes/(ms/1000.0))*60/(1024*1024)}')
    fi
    if (( decode_ms > 0 )); then
      decode_MB_per_min=$(awk -v bytes="$ORIG_SIZE_BYTES" -v ms="$decode_ms" 'BEGIN{print (bytes/(ms/1000.0))*60/(1024*1024)}')
    fi

    # Write CSV row
    echo "$level,\"${flags[*]}\",$encode_ms,$decode_ms,$encode_MB_per_min,$decode_MB_per_min,$compression_ratio,$COMPRESSED_SIZE_BYTES,$md5_ok,$BMP_PATH" >> "$COMPRESS_RESULTS_CSV"

    # Restore original binary for next level
    cp -f "${CFX}.backup" "$CFX"
    rm -f "${CFX}.backup"
  done

  echo "Compression test results written to $COMPRESS_RESULTS_CSV"
  column -t -s, "$COMPRESS_RESULTS_CSV" 2>/dev/null || cat "$COMPRESS_RESULTS_CSV"
  exit 0
fi

# Original test mode (flag combination tests using only INPUT_LABEL)
echo "Running original flag combination tests..."

# Results header
echo "combo,flags,encode_ms,decode_ms,encode_MB_per_min,decode_MB_per_min,md5_ok,bmp_path" > "$RESULTS_CSV"

# Iterate all combinations (gs ∈ {0,1}, noenc ∈ {0,1}, fs_mode ∈ {default-AIO, old-2f})
for gs in 0 1; do
  for noenc in 0 1; do
    for fs_mode in "aio" "2f"; do
      combo="gs=${gs},fs_mode=${fs_mode},noenc=${noenc}"

      # Cleanup previous artifacts
      rm -f "$BMP_HOME" "$BMP_LINK" 2>/dev/null || true

      # Build flags
      flags=()
      [[ "$gs" -eq 1 ]] && flags+=("-gs")
      [[ "$fs_mode" == "2f" ]] && flags+=("-2f")       # old two-filesystem
      [[ "$noenc" -eq 1 ]] && flags+=("-ne")

      # Encode from INPUT_LABEL
      needs_pw_encode="no"
      if [[ "$fs_mode" == "aio" && "$noenc" -eq 0 ]]; then
        needs_pw_encode="yes"
      fi

      start_ns=$(now_ns)
      run_cmd "$needs_pw_encode" "$CFX" -f "$INPUT_LABEL" "${flags[@]}"
      end_ns=$(now_ns)
      encode_ns=$((end_ns - start_ns))
      encode_ms=$((encode_ns / 1000000))

      # Determine BMP path
      BMP_PATH="$(get_bmp_path)"
      if [[ ! -f "$BMP_PATH" ]]; then
        echo "Combo $combo: BMP not found at $BMP_HOME nor $BMP_LINK"
        echo "$combo,\"${flags[*]}\",$encode_ms,NA,NA,NA,FAIL-BMP-NOT-FOUND,$BMP_PATH" >> "$RESULTS_CSV"
        continue
      fi

      # Backup the original binary, then run decode
      cp -f "$CFX" "${CFX}.backup"

      # Decode from BMP
      needs_pw_decode="$needs_pw_encode"
      start_ns=$(now_ns)
      run_cmd "$needs_pw_decode" "$CFX" -i "$BMP_PATH"
      end_ns=$(now_ns)
      decode_ns=$((end_ns - start_ns))
      decode_ms=$((decode_ns / 1000000))

      # Verify MD5 integrity
      NEW_MD5=$(md5sum "$DECODED_PATH" | awk '{print $1}')
      md5_ok="FAIL"
      [[ "$NEW_MD5" == "$ORIG_MD5" ]] && md5_ok="OK"

      # Throughput (MiB/min)
      encode_MB_per_min="NA"
      decode_MB_per_min="NA"
      if (( encode_ms > 0 )); then
        encode_MB_per_min=$(awk -v bytes="$ORIG_SIZE_BYTES" -v ms="$encode_ms" 'BEGIN{print (bytes/(ms/1000.0))*60/(1024*1024)}')
      fi
      if (( decode_ms > 0 )); then
        decode_MB_per_min=$(awk -v bytes="$ORIG_SIZE_BYTES" -v ms="$decode_ms" 'BEGIN{print (bytes/(ms/1000.0))*60/(1024*1024)}')
      fi

      # Write CSV row
      echo "$combo,\"${flags[*]}\",$encode_ms,$decode_ms,$encode_MB_per_min,$decode_MB_per_min,$md5_ok,$BMP_PATH" >> "$RESULTS_CSV"

      # Restore original binary for next combo
      cp -f "${CFX}.backup" "$CFX"
      rm -f "${CFX}.backup"
    done
  done
done

# Pretty print results
echo "Results written to $RESULTS_CSV"
column -t -s, "$RESULTS_CSV" 2>/dev/null || cat "$RESULTS_CSV"
