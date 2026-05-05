| Supported Targets | ESP32 |
| ----------------- | ----- |

# esp-amy-synth
IDF-native firmware for the esp-pocket-audio ESP32-A1S hardware.


### Prereqisites

#### ESP-IDF
Use ESP-IDF directly.

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
