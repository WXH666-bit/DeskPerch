$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$taskRoot=Split-Path $PSScriptRoot -Parent
function Rounded($g,$color,$x,$y,$w,$h,$r) {
    $p=[Drawing.Drawing2D.GraphicsPath]::new()
    $d=2*$r
    $p.AddArc($x,$y,$d,$d,180,90);$p.AddArc($x+$w-$d,$y,$d,$d,270,90)
    $p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90);$p.AddArc($x,$y+$h-$d,$d,$d,90,90)
    $p.CloseFigure();$b=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($color))
    $g.FillPath($b,$p);$b.Dispose();$p.Dispose()
}
$taskImages=[Collections.Generic.List[byte[]]]::new()
$taskSizes=@(16,20,24,32,40,48,64,128,256)
foreach($taskSize in ($taskSizes+512)) {
    $taskBitmap=[Drawing.Bitmap]::new($taskSize,$taskSize,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $taskGraphics=[Drawing.Graphics]::FromImage($taskBitmap)
    $taskGraphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $taskGraphics.Clear([Drawing.Color]::Transparent)
    $taskGraphics.ScaleTransform($taskSize/512.0,$taskSize/512.0)
    Rounded $taskGraphics '#cbd3dd' 32 32 448 448 104
    Rounded $taskGraphics '#fbfcfe' 36 36 440 440 100
    Rounded $taskGraphics '#7e8a99' 136 156 240 36 18
    Rounded $taskGraphics '#7e8a99' 136 236 160 36 18
    $taskBrush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#b4bdc9'))
    $taskGraphics.FillEllipse($taskBrush,320,312,64,64)
    $taskBrush.Dispose();$taskGraphics.Dispose()
    $taskStream=[IO.MemoryStream]::new()
    $taskBitmap.Save($taskStream,[Drawing.Imaging.ImageFormat]::Png)
    if($taskSize -eq 512){[IO.File]::WriteAllBytes((Join-Path $taskRoot 'assets\deskperch.png'),$taskStream.ToArray())}
    else{$taskImages.Add($taskStream.ToArray())}
    $taskBitmap.Dispose();$taskStream.Dispose()
}
$taskFile=[IO.File]::Create((Join-Path $taskRoot 'assets\deskperch.ico'))
$taskWriter=[IO.BinaryWriter]::new($taskFile)
$taskWriter.Write([uint16]0);$taskWriter.Write([uint16]1);$taskWriter.Write([uint16]$taskSizes.Count)
$taskOffset=6+16*$taskSizes.Count
for($taskIndex=0;$taskIndex -lt $taskSizes.Count;$taskIndex++) {
    $taskDimension=if($taskSizes[$taskIndex] -eq 256){0}else{$taskSizes[$taskIndex]}
    $taskWriter.Write([byte]$taskDimension);$taskWriter.Write([byte]$taskDimension)
    $taskWriter.Write([byte]0);$taskWriter.Write([byte]0)
    $taskWriter.Write([uint16]1);$taskWriter.Write([uint16]32)
    $taskWriter.Write([uint32]$taskImages[$taskIndex].Length);$taskWriter.Write([uint32]$taskOffset)
    $taskOffset+=$taskImages[$taskIndex].Length
}
foreach($taskBytes in $taskImages){$taskWriter.Write($taskBytes)}
$taskWriter.Dispose();$taskFile.Dispose()
