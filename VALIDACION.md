# Validación de 0.1 — experimental

Pruebas realizadas el 16 de octubre de 2026, en una placa ideaspark ESP32-D0WD con flash de 4 MiB y pantalla ST7789 170 × 320.

| Comprobación | Resultado |
|---|---|
| Copia original completa antes de modificar | 4 MiB, tamaño y SHA-256 comprobados; copia privada conservada fuera de este repositorio |
| Compilación ESP-IDF / NimBLE | Correcta; aplicación de 579264 bytes |
| Instalación | Esptool verificó los hashes de bootloader, particiones y aplicación |
| Pantalla y control físico | El usuario confirmó «BLE SCOUT» visible y cambio de vistas al pulsar y soltar BOOT |
| Receptor pasivo | Recepción de anuncios reales, sin conexiones automáticas |
| Parser en la placa | 9 vectores y controles de entradas inválidas correctos; informe en tests/parser-board.json |
| Guardas USB | Rechaza conexión/lectura sin confirmación, selección inválida y comando de escritura; recupera una línea excesiva |
| Cliente USB | 5 tests de transporte correctos en el PC de desarrollo |
| GATT, primer ensayo con periférico temporal del PC | Enumeró 7 servicios y 36 características; leyó el valor de prueba abierto; la lectura protegida fue rechazada con estado 261, sin emparejamiento |
| Reconexión GATT al fixture Windows | **Falló en ensayos posteriores**, con desconexión/código 574. No se ha aislado todavía la causa entre interoperabilidad del periférico Windows y cliente. No se considera validada la reconexión |
| Escrituras y permisos W | Comando de escritura no disponible. La propiedad W del fixture Windows no quedó validada; una propiedad declarada nunca demuestra permiso efectivo |
| Capturas | Cinco vistas obtenidas del framebuffer real. Se ocultaron nombres/direcciones en las versiones públicas |

El primer ensayo GATT no completó su aserción sobre W, por lo que **no se declara pasada la batería GATT completa**. `tests/gatt-local.json` conserva el resultado fallido del último ensayo, sin datos de terceros. Las capturas actuales muestran el estado posterior a esos ensayos; una vista sin atributos no es una enumeración simulada.

La tarea de GitHub Actions ejecuta los tests del cliente y un harness C con sanitizadores. Antes de su primera ejecución, el harness nativo no se había ejecutado en este PC Windows. Consulta el resultado real de Actions en GitHub.

Pendiente: corregir/aislar la reconexión, repetir con periféricos BLE propios de otros fabricantes, probar sesiones largas, validar límites con muchos anuncios, y comprobar otras revisiones de placa. No se ha validado un detector de ataques, un localizador ni una auditoría exhaustiva de seguridad.
