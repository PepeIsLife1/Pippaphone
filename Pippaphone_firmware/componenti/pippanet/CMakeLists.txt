# PippaNet: protocollo di rete (logica pura, uguale su ESP32 e nei test sul PC).
idf_component_register(SRCS "src/packet.cpp"
                            "src/charset.cpp"
                            "src/crypto.cpp"
                            "src/airtime.cpp"
                            "src/payloads.cpp"
                            "src/node.cpp"
                            "src/repeater.cpp"
                            "src/phone.cpp"
                            "src/platform_esp.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES monocypher esp_hw_support)
