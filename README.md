| Supported Targets | ESP32 |
| ----------------- | ----- |

# esp-amy-synth
Synthesizer Firmware for ESP32-A1S based on ESP-ADF and amy.


### Prereqisites

#### ESP-ADF
In this project esp-adf v2.8 is used with esp-idf v5.5.1. Replace the paths appropriately

- Through the VS-CODE extension
- Set the esp_adf path in the extension settings
- Apply the patches
    ```sh
    cd C:\Users\maxan\esp\v5.5.1\esp-idf\
    git apply C:/Users/maxan/esp/esp-adf/idf_patches/idf_v5.5_freertos.patch
    ```

#### Python environment
```sh
python -m venv .venv
.\.venv\Scripts\activate
cd components\amy\amy
git fetch --tags
git checkout tags/1.2.4
```

#### Install amy python library
```
cd components/amy/amy

git fetch --tags
git checkout tags/1.2.4

pip install -r requirements.txt
pip install -e .
```