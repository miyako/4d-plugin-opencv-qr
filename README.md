![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-opencv-qr)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-opencv-qr/total)

# 4d-plugin-opencv-qr
Decode QR code with OpenCV.

#### Syntax

```4d
status:=opencv decode qrcode(image{;epsilon})
```

Property|Type|Description
------------|------|----
status | Object|
status.success | Boolean|
status.values | Collection of Text|
status.images | Collection of Pictures|
status.corners | Collection of Objects|

[miyako.github.io](https://miyako.github.io/2022/06/03/4d-plugin-opencv-qr.html)
