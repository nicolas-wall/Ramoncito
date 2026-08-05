# Recorre las 13 expresiones y baja el framebuffer de cada una.
# 'e' avanza de expresion, 'f' vuelca la pantalla en hexadecimal.
# Se espera entre una y otra para que termine la animacion de entrada
# (INTRO ~400 ms) y la cara quede en su pose estable.
$dir = "C:\Users\nicow\AppData\Local\Temp\claude\D--Claude-espToy\5c5ad310-81a3-4422-aa51-8f2d061320cd\scratchpad\caras"
Remove-Item $dir -Recurse -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dir | Out-Null

$p = New-Object System.IO.Ports.SerialPort COM5,115200,None,8,one
$p.DtrEnable = $false; $p.ReadTimeout = 400; $p.NewLine = "`n"
$p.Open(); Start-Sleep -Milliseconds 600; $p.DiscardInBuffer()

for ($i = 0; $i -lt 13; $i++) {
    $p.WriteLine("e")
    Start-Sleep -Milliseconds 1500      # dejar asentar el INTRO
    $p.DiscardInBuffer()
    $p.WriteLine("f")

    $nombre = "?"
    $hex = New-Object System.Text.StringBuilder
    $enBloque = $false
    $fin = (Get-Date).AddSeconds(4)
    while ((Get-Date) -lt $fin) {
        try {
            $l = $p.ReadLine().Trim()
            if ($l -match '^\[cara\]\s+\d+/13\s+(\S+)') { $nombre = $matches[1] }
            elseif ($l -match '^\[fb\] inicio') { $enBloque = $true }
            elseif ($l -match '^\[fb\] fin')    { break }
            elseif ($enBloque -and $l -match '^[0-9A-F]+$') { [void]$hex.Append($l) }
        } catch {}
    }
    $txt = $hex.ToString()
    Set-Content -Path "$dir\$i.txt" -Value "$nombre`n$txt" -Encoding ascii
    Write-Output ("{0,2}  {1,-12} {2} chars" -f $i, $nombre, $txt.Length)
}
$p.Close()
