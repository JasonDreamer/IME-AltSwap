# IME AltSwap

AHKを使わず、左右Altキーの単押しでIMEを明示的に切り替えるWindows常駐アプリです。

- 左Alt単押し: IME OFF（英数）
- 右Alt単押し: IME ON（かな）
- Altを押しながら別のキーを操作: 通常のAltショートカット
- Altを500msより長く押した場合: IMEを切り替えない
- Altを押しながらマウスボタンやホイールを操作: IMEを切り替えない
- 左右Altを短い間隔で操作: 最後の切り替え要求を優先
- 初回起動時: 現在のユーザーのWindowsスタートアップへ自動登録
- タスクトレイ: 有効/無効、スタートアップ登録、終了を操作可能

## ビルド

Visual Studio 2022の「C++によるデスクトップ開発」とCMakeを使用します。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## インストール

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Install.ps1
```

`%LOCALAPPDATA%\Programs\IME-AltSwap`へコピーして起動し、現在のユーザーのログオン時に自動起動します。管理者権限は不要です。

## アンインストール

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Uninstall.ps1
```
