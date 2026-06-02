idf_component_register(
    SRCS "main.cpp" "BresenhamPID.cpp" "TemperatureReader.cpp"
         "ProfileEngine.cpp" "AITuner.cpp" "DisplayRenderer.cpp"
         "ButtonDebouncer.cpp" "Buzzer.cpp" "ADS1015_Driver.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer u8g2
)
