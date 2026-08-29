#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>

struct WavData {
  int sample_rate = 0;
  std::vector<float> samples;
};

// Helper: read little-endian value
template <typename T>
static T read_le(std::ifstream& f) {
  T value;
  f.read(reinterpret_cast<char*>(&value), sizeof(T));
  return value;
}

// Helper: write little-endian value
template <typename T>
static void write_le(std::ofstream& f, T value) {
  f.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

inline WavData wav_read_mono(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open file: " + path);
  }

  // Read RIFF header
  char riff[4];
  file.read(riff, 4);
  if (std::string(riff, 4) != "RIFF") {
    throw std::runtime_error("Not a RIFF file");
  }

  uint32_t riff_size = read_le<uint32_t>(file);
  (void)riff_size; // suppress unused warning

  char wave[4];
  file.read(wave, 4);
  if (std::string(wave, 4) != "WAVE") {
    throw std::runtime_error("Not a WAVE file");
  }

  // Parse chunks
  int sample_rate = 0;
  int num_channels = 0;
  int bits_per_sample = 0;
  std::vector<int16_t> sample_data;

  while (file.good()) {
    char chunk_id[4];
    file.read(chunk_id, 4);
    if (!file.good()) break;

    uint32_t chunk_size = read_le<uint32_t>(file);
    std::string id(chunk_id, 4);

    if (id == "fmt ") {
      // Parse format chunk
      uint16_t audio_format = read_le<uint16_t>(file);
      if (audio_format != 1) {
        throw std::runtime_error("Only PCM format (1) is supported");
      }
      num_channels = read_le<uint16_t>(file);
      sample_rate = read_le<uint32_t>(file);
      uint32_t byte_rate = read_le<uint32_t>(file);
      uint16_t block_align = read_le<uint16_t>(file);
      bits_per_sample = read_le<uint16_t>(file);
      (void)byte_rate;
      (void)block_align;

      if (bits_per_sample != 16) {
        throw std::runtime_error("Only 16-bit samples are supported");
      }

      // Skip remaining bytes in fmt chunk (for extended formats)
      uint32_t bytes_read = 16;
      if (chunk_size > bytes_read) {
        std::vector<char> skip(chunk_size - bytes_read);
        file.read(skip.data(), chunk_size - bytes_read);
      }

      // Handle odd-size padding
      if (chunk_size & 1) {
        char pad;
        file.read(&pad, 1);
      }
    } else if (id == "data") {
      // Read audio data
      uint32_t num_samples = chunk_size / (num_channels * 2); // 2 bytes per 16-bit sample
      sample_data.resize(num_samples * num_channels);
      for (uint32_t i = 0; i < num_samples * num_channels; i++) {
        sample_data[i] = read_le<int16_t>(file);
      }

      // Handle odd-size padding
      if (chunk_size & 1) {
        char pad;
        file.read(&pad, 1);
      }
      // Don't break; there might be more chunks after data
    } else {
      // Skip unknown chunk (LIST, etc.)
      std::vector<char> skip(chunk_size);
      file.read(skip.data(), chunk_size);

      // Handle odd-size padding
      if (chunk_size & 1) {
        char pad;
        file.read(&pad, 1);
      }
    }
  }

  if (sample_rate == 0 || sample_data.empty()) {
    throw std::runtime_error("Invalid WAV file: missing or empty audio data");
  }

  // Convert to mono float, averaging channels
  WavData result;
  result.sample_rate = sample_rate;
  uint32_t num_samples = sample_data.size() / num_channels;
  result.samples.resize(num_samples);

  for (uint32_t i = 0; i < num_samples; i++) {
    float sum = 0.0f;
    for (int ch = 0; ch < num_channels; ch++) {
      sum += static_cast<float>(sample_data[i * num_channels + ch]);
    }
    result.samples[i] = sum / (num_channels * 32768.0f);
  }

  return result;
}

inline void wav_write_mono(const std::string& path, int sample_rate,
                    const std::vector<float>& x) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open file for writing: " + path);
  }

  uint32_t num_samples = x.size();
  uint32_t byte_rate = sample_rate * 2; // 2 bytes per sample (16-bit mono)
  uint32_t data_size = num_samples * 2;
  uint32_t file_size = 36 + data_size; // 36-byte header + data

  // Write RIFF header
  file.write("RIFF", 4);
  write_le<uint32_t>(file, file_size);
  file.write("WAVE", 4);

  // Write fmt chunk
  file.write("fmt ", 4);
  write_le<uint32_t>(file, 16); // Subchunk1Size (16 for PCM)
  write_le<uint16_t>(file, 1);  // AudioFormat (1 = PCM)
  write_le<uint16_t>(file, 1);  // NumChannels (1 = mono)
  write_le<uint32_t>(file, sample_rate);
  write_le<uint32_t>(file, byte_rate);
  write_le<uint16_t>(file, 2);  // BlockAlign (2 bytes for 16-bit mono)
  write_le<uint16_t>(file, 16); // BitsPerSample (16)

  // Write data chunk
  file.write("data", 4);
  write_le<uint32_t>(file, data_size);

  // Write samples: clamp to [-1, 1], multiply by 32767, round to int16.
  // Rounding must match tools/ref_render.js's Math.round(x) on a double
  // product (Math.round(x) == floor(x+0.5)), computed here in double rather
  // than float, so negative-half ties round the same way as the JS
  // reference and the WAVs are bit-identical.
  for (uint32_t i = 0; i < num_samples; i++) {
    float clamped = std::clamp(x[i], -1.0f, 1.0f);
    int v = (int)std::floor((double)clamped * 32767.0 + 0.5);
    int16_t sample = static_cast<int16_t>(v);
    write_le<int16_t>(file, sample);
  }
}
