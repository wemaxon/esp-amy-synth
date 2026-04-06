| Supported Targets | ESP32 |
| ----------------- | ----- |

# esp-amy-synth


### Prereqisites

#### ESP-ADF
- Through the VS-CODE extension
- Set the esp_adf path in the extension settings

#### Python environment
```
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