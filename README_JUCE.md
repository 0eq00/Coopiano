# Coopiano — JUCE 8 Physical Modeling Piano Plugin (VST3 / AU / Standalone)

[peterc/coopiano](https://github.com/peterc/coopiano) の C++17 物理モデリング・ピアノシンセサイザー (`piano.hpp`, `instrument.hpp`, `piano_host.hpp`, `reverb.hpp`, `preset.hpp`) をベースにしたクロスプラットフォーム JUCE 8 プラグインプロジェクトです。

## 特徴と設計

- **ノンブロッキング・バックグラウンド再構築 (`EngineRuntime.h`)**:
  `coopiano` の `instrument::Engine` は構築時に 88鍵 × 127ベロシティの非線形ハンマー接触応答 (`StrikeBank`) と音響板 (Soundboard) インパルス応答を生成します。プラグインではこれを専用のバックグラウンドスレッド (`EngineBuilderThread`) でデバウンス生成し、Wait-Free なポインタ交換でオーディオスレッドへ反映するため、`processBlock()` 内でメモリ確保やロックが一切発生しません。
- **ゼロリビルドのリアルタイム・リバーブ & マスターゲイン**:
  `room::StereoReverb` をホストサンプルレート側で独立動作させることで、リバーブや出力ゲインの調整時に `StrikeBank` の再計算を発生させず、演奏中の音を途切れさせずに即座に音色を変化できます。
- **ホストサンプルレート適応 & DAW 遅延補正 (PDC)**:
  44.1 / 48 / 88.2 / 96 kHz では `piano_host::Adapter` の 257-tap Blackman-Harris Sinc リサンプラーを使用し、それ以外のサンプルレート (176.4 / 192 / 32 kHz 等) でも `Resampler.h` の 112-tap Kaiser 窓 Sinc 補間器で帯域制限リサンプリングを行います。正確なレイテンシを `setLatencySamples()` で DAW へ通知します。
- **37 パラメータ + `.ini` プリセット互換**:
  `default.ini` / `preset.hpp` と完全互換の全物理パラメータ (Hammer / Strings / Tone / Soundboard / Resonance / Reverb) を APVTS で公開し、DAW オートメーションおよび `.ini` ファイルの読み込み・書き出しに対応します。

## ビルド方法 (macOS / Windows / Linux)

必要要件:
- CMake 3.22 以上
- C++17 対応コンパイラ (Xcode 14+, Visual Studio 2022, GCC 10+ / Clang 13+)
- インターネット接続 (初回ビルド時に CMake `FetchContent` で JUCE 8.0.4 を自動取得します。ローカルの JUCE を使う場合は `-DCOOP_JUCE_DIR=/path/to/JUCE` を指定してください)

```bash
# Release ビルドの構成
cmake -B build -DCMAKE_BUILD_TYPE=Release

# コンパイル (VST3 / AU / Standalone)
cmake --build build --config Release -j
```

macOS で Universal Binary (Apple Silicon + Intel) をビルドする場合:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j
```

生成されたプラグインは `COPY_PLUGIN_AFTER_BUILD TRUE` により OS 標準のプラグインフォルダ (`~/Library/Audio/Plug-Ins/VST3` や `.../Components` 等) にも自動コピーされます。
