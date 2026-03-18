package com.myoem.thermalcontrol;
@VintfStability
interface IThermalControlService {
  float getCpuTemperatureCelsius();
  int getFanSpeedRpm();
  boolean isFanRunning();
  int getFanSpeedPercent();
  boolean isFanAutoMode();
  void setFanEnabled(boolean enabled);
  void setFanSpeed(int speedPercent);
  void setFanAutoMode(boolean autoMode);
  const int ERROR_HAL_UNAVAILABLE = 1;
  const int ERROR_INVALID_SPEED = 2;
  const int ERROR_SYSFS_WRITE = 3;
}
