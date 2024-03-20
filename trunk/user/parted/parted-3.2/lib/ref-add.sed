/^# Packages using this file: / {
  s/# Packages using this file://
  ta
  :a
  s/ parted / parted /
  tb
  s/ $/ parted /
  :b
  s/^/# Packages using this file:/
}
