param([string]$Path = ".\v08.log")
$rows = Get-Content $Path | Where-Object { $_ -match '^\d\d:\d\d:\d\d vbus=' } | ForEach-Object {
  $m = [regex]::Matches($_, 'vbus=(\d+) ibus=(\d+) vbat=(\d+) ibat=(-?\d+) soc=(\d+)')
  if ($m.Count -gt 0) {
    [pscustomobject]@{
      vbus = [double]$m[0].Groups[1].Value/1e6
      ibus = [double]$m[0].Groups[2].Value/1e6
      vbat = [double]$m[0].Groups[3].Value/1e6
      ibat = [double]$m[0].Groups[4].Value/1e3
      soc  = [int]$m[0].Groups[5].Value
    }
  }
}
$n = $rows.Count
$in9 = @($rows | Where-Object { $_.vbus -gt 7.0 }).Count
$hi = @($rows | Where-Object { $_.ibus -gt 0.5 }).Count
[pscustomobject]@{
  Samples = $n
  NineV_pct = [math]::Round(100*$in9/$n,1)
  Ibus_over_0_5A_pct = [math]::Round(100*$hi/$n,1)
  Vbus_avg = [math]::Round((($rows|Measure-Object vbus -Average).Average),3)
  Vbus_min = [math]::Round((($rows|Measure-Object vbus -Minimum).Minimum),3)
  Vbus_max = [math]::Round((($rows|Measure-Object vbus -Maximum).Maximum),3)
  Ibus_avg = [math]::Round((($rows|Measure-Object ibus -Average).Average),3)
  Vbat_avg = [math]::Round((($rows|Measure-Object vbat -Average).Average),3)
  Ibat_avg = [math]::Round((($rows|Measure-Object ibat -Average).Average),3)
  Pbat_W_2S = [math]::Round(2*(($rows|ForEach-Object{$_.vbat*$_.ibat}|Measure-Object -Average).Average),2)
} | Format-List
