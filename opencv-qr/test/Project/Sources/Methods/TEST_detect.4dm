//%attributes = {}
$file:=Folder:C1567(fk desktop folder:K87:19).file("調整後.png")

var $image : Picture

READ PICTURE FILE:C678($file.platformPath; $image)

$status:=opencv detect qrcode($image)