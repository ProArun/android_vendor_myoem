package com.myoem.pirdetector;
@VintfStability
interface IPirDetectorService {
  int getCurrentState();
  void registerCallback(com.myoem.pirdetector.IPirDetectorCallback callback);
  void unregisterCallback(com.myoem.pirdetector.IPirDetectorCallback callback);
  int getVersion();
}
