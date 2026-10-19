# GPLv3 for the C++ code

The firmware uses the ESPHome license split: C++ files are GPLv3, Python and YAML files
are MIT. The repo was MIT before. The change lets us start from the existing ESPHome CEC
code (GPLv3) and not write the bit-level part again. Every distributed ESPHome binary is
GPL anyway, so MIT on our C++ gives users no extra freedom.
