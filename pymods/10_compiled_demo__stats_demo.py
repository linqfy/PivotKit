import json, math, sys, platform

# Any pip-installed library works here too (numpy, requests, PySide...):
#     import numpy as np
data = {
    "python": platform.python_version(),
    "pi": round(math.pi, 10),
    "argv_mode": "compiled-block",
}
print(json.dumps(data))
