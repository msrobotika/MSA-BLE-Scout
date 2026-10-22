# MSA-BLE-Scout · Estado técnico

**Revisión:** 2026-10-22 · **Categoría:** firmware/hardware experimental

- Estado: Prototipo experimental BLE con comprobación en dispositivo.
- Licencia principal: archivo LICENSE, más avisos de dependencias en licenses/ cuando procede.
- Workflow existente: [.github/workflows/checks.yml](../.github/workflows/checks.yml). Su mera presencia no acredita validación de hardware nuevo.
- Capturas disponibles: [`docs/pantallas.png`](../docs/pantallas.png), [`docs/01-Radar.png`](../docs/01-Radar.png), [`docs/04-GATT.png`](../docs/04-GATT.png). Procedencia: framebuffer indicada por README.

## Riesgos / límites

Descubrimiento pasivo y consultas GATT autorizadas; las medidas RSSI no determinan una posición.

No probar sobre redes o dispositivos de terceros sin autorización. Registrar modelo de hardware, compilador, librerías, firmware exacto y logs anonimizados en cada validación. Ninguna alerta o lectura aislada demuestra intrusión ni ubicación física.
