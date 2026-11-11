# DLSS5 Geode

Filtro de **renderizado neuronal estilo DLSS 5** para Geometry Dash (Android 64 bits).

Compatible con **Geometry Dash 2.2081** y **Geode v5.11.2**.

Cada fotograma terminado se procesa en la GPU con una cadena de shaders. Hay cuatro calidades:

- **Rendimiento**: ligero, para moviles flojos.
- **Equilibrado**: el recomendado.
- **Ultra**: todos los efectos clasicos.
- **MAX**: el mas alto. Usa el shader nuevo.

## Que hace

- **Detalle neuronal**: nitidez adaptativa sin halos y claridad local.
- **Reiluminacion de materiales**: relieve, brillos y una luz que sigue al jugador.
- **Dispersion de luz**: bloom multinivel suave.
- **Sombras de contacto y AO**.
- **Etapa de color cinematografica**: curva filmica, vibrance, tintes, vineta y grano fino.

### Solo en MAX

- **Antialiasing de alta calidad** que sigue los bordes.
- **Bisel y reflejos** en bordes de bloques, picos y sierras.
- **Sombras suaves de pantalla completa** y oclusion ambiental con un mapa de objetos.
- **Iluminacion global**: las zonas oscuras reciben color del entorno.
- **Tono filmico que conserva el color** y nucleos de luz blancos como en una foto.

## Ajustes

- Calidad, estilo de color (Cinematico, Natural, Calido, Frio, Neon) y un slider por cada efecto.
- **Proteccion de FPS**: baja la calidad sola si el juego se pone lento.
- **Comparar antes/despues** dibuja una linea para ver la diferencia.
- En el menu de pausa hay un boton arriba a la izquierda para ciclar la calidad al instante.

## Importante

Esto **no es el DLSS 5 real de NVIDIA**: ese necesita una GPU RTX, un modelo de IA y datos 3D del motor del juego. Aqui es una aproximacion visual hecha con shaders propios, pensada para verse lo mas parecida posible.

MAX pide un movil potente. Si tu GPU no lo soporta, el mod baja solo a Ultra. Si algo falla, el filtro se apaga y el juego sigue normal.
