# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-aoe-loot

[English](README.md) | [Español](README_ES.md)

[![Build Status](https://github.com/azerothcore/mod-aoe-loot/workflows/core-build/badge.svg?branch=master&event=push)](https://github.com/azerothcore/mod-aoe-loot/actions)

## Descripción

Este módulo habilita el saqueo en área (AOE) para AzerothCore. Cuando un jugador saquea un cadáver, todos los cadáveres cercanos elegibles se saquean automáticamente — los objetos van directamente a la bolsa del jugador sin necesidad de hacer clic en cada cuerpo individualmente.

## Requisitos

- AzerothCore rama master (versión reciente)
- PR del core [feat/player-creature-loot-opened-hook](https://github.com/azerothcore/azerothcore-wotlk/pull/XXXX) aplicado *(agrega el hook `OnPlayerCreatureLootOpened` y el método `Player::AutoTakeCreatureLoot` utilizados por este módulo)*

## Instalación

### 1. Aplicar el PR del core

Este módulo depende de dos adiciones al core de AzerothCore. Asegurarse de que el PR indicado arriba esté aplicado antes de compilar.

### 2. Clonar el módulo

```bash
cd <ACoreDir>/modules
git clone https://github.com/azerothcore/mod-aoe-loot.git
```

### 3. Recompilar AzerothCore

```bash
cd <ACoreDir>/build
cmake .. && make -j$(nproc) && make install
```

### 4. Configurar

Copiar `conf/mod_aoe_loot.conf.dist` al directorio de configuración del servidor y ajustar los valores según sea necesario (ver **Configuración** más abajo).

### 5. Reiniciar el worldserver

---

## Cómo funciona

Cuando un jugador saquea cualquier cadáver:

1. El servidor aplica sus reglas normales de loot a ese cadáver (asignación round-robin, rolls, etc.).
2. El módulo busca cadáveres saqueables cercanos dentro del rango configurado.
3. Para cada cadáver cercano elegible, el módulo llama a la rutina interna de toma de ítems del servidor en nombre del jugador. Los ítems a los que el jugador tiene derecho van directamente a su bolsa; la ventana de loot normal permanece abierta solo para el cadáver en el que el jugador hizo clic.

Los ítems llegan al inventario con la notificación estándar "Recibes botín" — sin ventanas de loot adicionales ni merging de ítems.

---

## Comportamiento en grupo

> **Importante para administradores de servidor y jugadores**

Este módulo respeta completamente el método de botín configurado para el grupo.

### Botín de Grupo / Turno Rotatorio / Necesito antes que Codicia

El servidor asigna cada cadáver a un miembro específico del grupo mediante su rotación round-robin (establecida al morir la criatura, antes de que cualquier jugador abra el botín). El módulo respeta esta asignación:

- **Cada jugador recolecta automáticamente solo los cadáveres que le fueron asignados** por la rotación. Los cadáveres de los demás miembros se dejan intactos y siguen siendo saqueables.
- **Los ítems van directamente a la bolsa del jugador asignado**, incluso si ese jugador no hizo clic en el cadáver. Esto ocurre automáticamente cuando cualquier miembro del grupo saquea algo cercano.
- **Si un jugador abre manualmente un cadáver asignado a otro miembro**, los ítems del dueño legítimo se envían inmediata y silenciosamente a su bolsa antes de que el que abrió el cadáver pueda interactuar con la ventana de botín. El que abrió el cadáver no recibe nada de él.

En la práctica: cada jugador necesita saquear al menos un cadáver para activar la recolección automática de todos los cadáveres que le fueron asignados. Los jugadores verán ítems apareciendo en su bolsa provenientes de cadáveres en los que nunca hicieron clic.

### Libre Para Todos

Todos los miembros del grupo recolectan de todos los cadáveres cercanos elegibles sin restricciones.

### Maestro del Botín

El maestro del botín recolecta automáticamente todos los cadáveres cercanos elegibles.

---

## Comandos de jugador

| Comando | Descripción |
|---------|-------------|
| `.aoeloot on` | Activar el saqueo AOE para tu personaje |
| `.aoeloot off` | Desactivar el saqueo AOE para tu personaje |

> Las preferencias del jugador se reinician al cerrar sesión. El saqueo AOE está activado por defecto cuando el módulo está activo.

---

## Configuración

| Opción | Tipo | Predeterminado | Descripción |
|--------|------|----------------|-------------|
| `AOELoot.Enable` | Booleano | 1 | Activar o desactivar el módulo globalmente |
| `AOELoot.Message` | Booleano | 1 | Mostrar mensaje informativo al iniciar sesión |
| `AOELoot.Range` | Float | 55.0 | Radio máximo de búsqueda en yardas (5.0 – 100.0) |
| `AOELoot.Group` | Booleano | 1 | Activar el saqueo AOE cuando el jugador está en grupo |
| `AOELoot.MaxCorpses` | Entero | 20 | Máximo de cadáveres cercanos procesados por activación (1 – 50) |

### Recomendado: decay de cadáveres más rápido

Para evitar desorden visual con los cadáveres saqueados permaneciendo en el suelo, reducir el tiempo de desaparición en `worldserver.conf`:

```conf
# Predeterminado: 0.5 — Recomendado para saqueo AOE: 0.01
Rate.Corpse.Decay.Looted = 0.01
```

---

## Solución de problemas

| Problema | Solución |
|----------|----------|
| El saqueo AOE no se activa | Verificar `AOELoot.Enable = 1` y `.aoeloot on` en tu personaje |
| Solo se saquea un cadáver | Verificar que el PR del core esté aplicado y el servidor recompilado |
| Los ítems no van al jugador correcto | Confirmar que el método de botín del grupo esté configurado antes del combate |
| Los cadáveres no desaparecen | Configurar `Rate.Corpse.Decay.Looted = 0.01` en `worldserver.conf` |

---

## Limitaciones conocidas

- Las preferencias de activación/desactivación (`.aoeloot on/off`) no persisten entre sesiones.
- El saqueo AOE se activa solo cuando un jugador saquea un cadáver; los jugadores inactivos cerca de un combate no recolectan automáticamente.

---

## Créditos

- **acidmanifesto** — [Autor original y concepto](https://github.com/azerothcore/mod-aoe-loot/pull/2)
- **Comunidad AzerothCore** — Hooks, actualizaciones y mejoras
- **Colaboradores** — Comandos de jugador, soporte multi-idioma y corrección de errores

## Enlaces

- **AzerothCore:** [Repositorio](https://github.com/azerothcore) | [Sitio web](https://azerothcore.org/) | [Discord](https://discord.gg/PaqQRkd)
- **Repositorio del módulo:** [GitHub](https://github.com/azerothcore/mod-aoe-loot)
- **Problemas y sugerencias:** [Issue Tracker](https://github.com/azerothcore/mod-aoe-loot/issues)

## Licencia

Este módulo se distribuye bajo la [Licencia GNU AGPL v3](https://github.com/azerothcore/mod-aoe-loot/blob/master/LICENSE).
