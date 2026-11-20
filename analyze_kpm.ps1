param([string]$Path = ".\kpm_run_240.log")
$rows = Get-Content $Path | Where-Object { $_ -match '^\d\d:\d\d:\d\d ' } | ForEach-Object {
  $p = $_ -split '\s+'
  [pscustomobject]@{
    t=$p[0]; vbat=[double]$p[1]/1e6; ibat=[double]$p[2]/1e3
    vbus=[double]$p[3]/1e6; ibus=[double]$p[4]/1e6
    temp=[double]$p[5]/10; soc=[int]$p[6]; rt=$p[8]; ib=$p[9]
  }
}
$n = $rows.Count
$in9 = @($rows | Where-Object { $_.vbus -gt 7.0 }).Count
$avg = {
  param($prop)
  [math]::Round((($rows | Measure-Object $prop -Average).Average), 3)
}
$pin = [math]::Round(($rows | ForEach-Object { $_.vbus * $_.ibus } | Measure-Object -Average).Average, 2)
$pbat = [math]::Round(($rows | ForEach-Object { 2 * $_.vbat * $_.ibat } | Measure-Object -Average).Average, 2)
[pscustomobject]@{
  Samples = $n
  '9V_samples_pct' = [math]::Round(100*$in9/$n,1)
  Vbus_V = & $avg vbus
  Ibus_A = & $avg ibus
  Pin_W = $pin
  Vbat_V = & $avg vbat
  Ibat_A = & $avg ibat
  Pbat_W = $pbat
  Temp_start = $rows[0].temp
  Temp_end = $rows[-1].temp
  SOC_start = $rows[0].soc
  SOC_end = $rows[-1].soc
  Minutes = [math]::Round($n/60.0, 1)
} | Format-List
Write-Output '--- KPM log ---'
Get-Content $Path | Select-String -Pattern 'kpm-charge' | Select-Object -Last 6 | ForEach-Object { $_.ToString() }
Write-Output '--- pdo lines ---'
Get-Content $Path | Select-String -Pattern 'pdo_select|charger voltage' | Select-Object -Last 6 | ForEach-Object { $_.ToString() }
