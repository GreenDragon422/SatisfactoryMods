# Satisfactory Mods

Source code and assets for my Satisfactory mods.

This repository is a curated public export of the development workspace. Generated binaries, build output, local tooling, and other development-only files are not included.

## Released

### [Truck Station Signs](TruckStationSigns)

Automatically displays the player-assigned name of each standard or Fluid Truck Station on a centered vanilla 8x1 sign at the front of the station. The generated sign is transient, is not stored in the save, and is removed with its station.

## In Development

### [Belt Purge](BeltPurge)

Provides a deliberately destructive cleanup action for connected conveyor networks. Pressing `Ctrl+Shift+Delete` while aiming at a belt starts a server-authoritative purge that follows connected belts through conveyor attachments and removes belt items, attachment buffers, and fed machine input inventories in bounded batches.

### [Codex Bridge](CodexBridge)

Connects Satisfactory to Codex Desktop through a local companion service and a secured named pipe. In-game console commands can send a message to a dedicated Codex task, report the bridge connection status, and start a new task for the next message.

### [Conveyor Signs](ConveyorSigns)

Adds one centered wall-sign attachment position to the left, right, and rear faces of the logical input housing on every vanilla Conveyor Lift tier. The player configures the input-side sign, and the mod mirrors that display on the matching face of the output housing while keeping dismantling behavior synchronized.

### [PathFinder](PathFinder)

Provides vehicle pathfinding debugging tools for Satisfactory. The current work focuses on tracing a route leg for a selected vehicle and visualizing the useful A* path and search frontier so vehicle navigation behavior can be inspected in game.

## Repository contents

Each mod directory contains the source files, configuration, content, resources, and runtime components selected for public publication. Development happens in a separate private workspace; this repository is updated only through its curated publishing workflow.
