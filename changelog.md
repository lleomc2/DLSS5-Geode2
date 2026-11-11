# v1.2.0

- Actualizado a **Geometry Dash 2.2091** y **Geode v6.0.0-alpha.1** (antes solo funcionaba con GD 2.2081 y Geode 5, y por eso salia como version antigua).
- Comprobado contra el loader de la alpha.1 y contra el nightly: usa solo funciones que existen en los dos.

# v1.1.0

- Nueva calidad **MAX** con un shader nuevo: antialiasing de alta calidad, bisel y reflejos, sombras suaves de pantalla completa, oclusion ambiental, iluminacion global y tono filmico que conserva el color.
- Todas las calidades: mejor manejo de las luces fuertes (nucleos blancos, sin cambios de color).
- Nuevos ajustes: reflejos, iluminacion global y proteccion de FPS.
- Si la GPU no puede con MAX o Ultra, baja sola al siguiente nivel.
- El boton de pausa ahora cicla hasta MAX.

# v1.0.0

- Reescrito desde cero: filtro de posprocesado real en GPU.
- Detalle, reiluminacion, bloom multinivel, sombras de contacto, AO y color cinematografico.
- Tres calidades, cinco estilos de color y control fino de cada efecto.
- Boton rapido en el menu de pausa y comparador antes/despues.
- Proteccion: si la GPU da error, el filtro se desactiva solo.
