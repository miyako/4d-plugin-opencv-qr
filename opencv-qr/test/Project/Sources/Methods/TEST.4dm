//%attributes = {}
$file:=Folder:C1567(fk resources folder:K87:11).file("sample.png")

$file:=Folder:C1567(fk desktop folder:K87:19).file("調整後.jpg")

var $image : Picture

READ PICTURE FILE:C678($file.platformPath; $image)

$status:=opencv decode qrcode($image; 0.5)

If ($status.success)
	
	For each ($image; $status.images)
		
		SET PICTURE TO PASTEBOARD:C521($image)
		
	End for each 
End if 