![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-opencv-qr)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-opencv-qr/total)

`opencv-qr` adds two 4D language commands that use OpenCV's QR code engine to locate and, optionally, decode QR codes found in a **Picture**. Both commands accept the same parameters and can find **multiple QR codes in a single image** in one call.

Both commands are declared `threadSafe` in the plugin manifest, so they can be called from preemptive processes as well as the main process.

| Command | Decodes content? | Use when… |
|---|---|---|
| [`opencv decode qrcode`](#opencv-decode-qrcode) | Yes | You need the text stored in the QR code(s) |
| [`opencv detect qrcode`](#opencv-detect-qrcode) | No | You only need to know *if/where* a QR code is (faster, skips decoding) |

---

## opencv decode qrcode

```4d
$status:=opencv decode qrcode(picture) // epsilon omitted
$status:=opencv decode qrcode(picture; epsilon)
```

Locates every QR code in `picture`, decodes its content, and returns a corrected (straightened) image of each code found.

### Parameters

| Parameter | Type | | Description |
|---|---|---|---|
| `picture` | Picture | → | The image to scan. |
| `epsilon` | Real | → | *Optional.* Corner-detection tolerance forwarded to OpenCV's `setEpsX`/`setEpsY`. Leave it out (or pass `0`) to use OpenCV's own default — this is what you want in almost every case. Only adjust it if detection is failing on a specific image and you know what you're tuning. |
| Result | Object | ← | Status object, see below. |

### Result object

| Key | Type | Present when | Description |
|---|---|---|---|
| `success` | Boolean | always | `True` if at least one QR code was found and decoded. |
| `error` | Text | only on an internal failure | Diagnostic message (e.g. the picture couldn't be read). If this key exists, `success` is `False`. |
| `values` | Collection of Text | `success` is `True` | The decoded string content of each QR code, in detection order. |
| `images` | Collection of Picture | `success` is `True` | The perspective-corrected ("straightened") image of each QR code, in the same order as `values`. |
| `corners` | Collection of Object | `success` is `True` | Corner points of every detected QR code, flattened across all codes: 4 consecutive `{x; y}` points per code (one quadrilateral each), in the order OpenCV reports them. |

Each entry in `corners` is an Object with two properties:

| Key | Type | Description |
|---|---|---|
| `x` | Real | X coordinate, in pixels |
| `y` | Real | Y coordinate, in pixels |

### Sample code

```4d
// Read a picture from disk, decode any QR codes in it,
// and paste each straightened QR code image onto the pasteboard.

$file:=Folder(fk resources folder).file("sample.png")

var $image : Picture
READ PICTURE FILE($file.platformPath; $image)

$status:=opencv decode qrcode($image)

If ($status.success)
	
	For each ($image; $status.images)
		SET PICTURE TO PASTEBOARD($image)
	End for each 
	
	// $status.values holds the matching decoded text, same order as $status.images
	For each ($text; $status.values)
		ALERT($text)
	End for each 
	
Else 
	
	If ($status.error#Null)
		ALERT("QR decoding failed: "+$status.error)
	End if 
	
End if 
```

---

## opencv detect qrcode

```4d
$status:=opencv detect qrcode(picture) // epsilon omitted
$status:=opencv detect qrcode(picture; epsilon)
```

Locates every QR code in `picture` — returning their corner positions — **without** decoding their content. Use this when you only need to confirm presence/position of QR codes (for example, to draw bounding boxes, or to decide whether it's worth calling [`opencv decode qrcode`](#opencv-decode-qrcode) at all), since skipping the decode step is faster.

### Parameters

Same as [`opencv decode qrcode`](#opencv-decode-qrcode) above: `picture` (required), `epsilon` (optional).

### Result object

| Key | Type | Present when | Description |
|---|---|---|---|
| `success` | Boolean | always | `True` if at least one QR code was located. |
| `error` | Text | only on an internal failure | Same meaning as above. |
| `corners` | Collection of Object | `success` is `True` | Same format as [`opencv decode qrcode`](#opencv-decode-qrcode)'s `corners` (4 points per detected code). |

Note: `values` and `images` are **not** returned by this command, since no decoding takes place.

### Sample code

```4d
// Read a picture from disk and just check whether it contains a QR code.

$file:=Folder(fk desktop folder).file("調整後.jpg")

var $image : Picture
READ PICTURE FILE($file.platformPath; $image)

$status:=opencv detect qrcode($image)

If ($status.success)
	ALERT("Found "+String($status.corners.length/4)+" QR code(s)")
Else 
	ALERT("No QR code found in this image")
End if 
```

---

## Notes for both commands

- **Multiple codes per image**: both commands are "multi" detectors — if a picture contains several QR codes, every one of them is reported. `corners` (and, for `decode`, `values`/`images`) will contain one group of results per code found.
- **Reading `corners`**: since points are flattened 4-per-code, the corner group for the *n*-th code (0-based) is `$status.corners[(n*4)] … $status.corners[(n*4)+3]`.
- **No QR code found**: this is *not* an error — `success` will simply be `False` with no `error` key set. Only use `error` to detect actual failures (bad/unreadable picture, internal exception).
- **Picture format**: pass any Picture 4D can hold in a picture variable/field; the plugin converts it internally as needed before scanning.
