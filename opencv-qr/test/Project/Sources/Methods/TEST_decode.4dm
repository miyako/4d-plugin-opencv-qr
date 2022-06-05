//%attributes = {}
$file:=Folder:C1567(fk resources folder:K87:11).file("sample.png")

var $image : Picture

READ PICTURE FILE:C678($file.platformPath; $image)

$status:=opencv decode qrcode($image)

If ($status.success)
	
	For each ($image; $status.images)
		
		SET PICTURE TO PASTEBOARD:C521($image)
		
	End for each 
End if 