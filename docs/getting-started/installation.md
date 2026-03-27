## 1. Clone the Repo 

To get started with AromaUI, you need to clone the repository from GitHub. Open your terminal or command prompt and run the following command:

```bash
git clone https://github.com/BinaryInkTN/AromaUI.git --recursive
```

## 2. Add aroma.py to PATH
Once the download is complete, locate the `aroma.py` file (usually in the tools/cli folder) and add its location to your system's PATH environment variable. This will allow you to run the `aroma.py` command from any location in your terminal or command prompt.

## 3. Verify Installation

To verify that you have the correct dependencies installed and that AromaUI is properly set up, you can run the following command:

```shell
yassine@DESKTOP-4SJS544:~/AromaUI$ python3 aroma.py doctor
==> Running Aroma Doctor...
OS: Linux 6.6.87.2-microsoft-standard-WSL2
✓ CMake installed
✓ Ninja: 1.11.1
✓ GCC installed
✓ Java installed
✓ keytool installed
✓ Android SDK: /home/yassine/Android/Sdk
✓ Android NDK: /home/yassine/Android/Sdk/ndk/25.1.8937393
==> Doctor summary complete.

```

> If you don't have the SDK or NDK installed run the following command to install them:

```bash
python3 aroma.py install android-sdk
```