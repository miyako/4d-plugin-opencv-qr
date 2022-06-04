![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-opencv-qr)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-opencv-qr/total)

# 4d-plugin-opencv-qr
Decode QR code with OpenCV.

#### Syntax

```4d
status:=opencv decode qrcode(image)
```

<div class="grid">
<div class="syntax-th cell cell--2">Parameter</div>
<div class="syntax-th cell cell--2">Type</div>
<div class="syntax-th cell cell--8">Description</div>
<div class="syntax-td cell cell--2">image</div>
<div class="syntax-td cell cell--2">PICTURE</div>
<div class="syntax-td cell cell--8">image containing QR codes</div>   
<div class="syntax-td cell cell--2">status</div>
<div class="syntax-td cell cell--2">OBJECT</div>
<div class="syntax-td cell cell--8"></div>   
</div>

Property|Type|Description
------------|------|----
status | Object|
status.success | Boolean|
status.values | Collection of Text|
status.images | Collection of Pictures (PNG)|

