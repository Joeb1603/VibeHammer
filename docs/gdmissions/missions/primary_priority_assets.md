# Primary Missions: Priority Assets deck

Source: https://gdmissions.app/11th/primary-missions/priority-assets

The Priority Assets force disposition scores one of these five missions, chosen by the opponent's force disposition (see [the mission matrix](../force_disposition.md)).

Flags column: `or` = alternative to the row above, `cumulative` = adds to the rows above, `perUnit` = scored per unit, `eob` = end-of-battle scoring.

## Extract Relic

**You:** Priority Assets · **Opponent:** Disruption · [card page](https://gdmissions.app/11th/primary-missions/priority-assets/extract-relic) · [layouts](../layouts.md#priority-assets-vs-disruption)

### ANY BATTLE ROUND

**When:** End of your turn

| Condition | VP | Flags |
|---|---|---|
| A friendly unit **performed a sensor sweep** this turn. | 4 |  |
| One or more enemy units that started the turn within range of one or more **objectives** are **destroyed**. | 3 |  |
| Only one of your opponent's **operation markers** is on the battlefield, if one or more of your units are within the same **terrain area** as that **operation marker**, and no enemy units are within that **terrain area**. | 4 |  |

### SECOND BATTLE ROUND ONWARDS

**When:** End of your Command phase (or the end of your turn in the fifth battle round)

| Condition | VP | Flags |
|---|---|---|
| You control one or more **objectives** (excluding your **home objective**). | 4 |  |

### END OF BATTLE

| Condition | VP | Flags |
|---|---|---|
| Only one of your opponent's **operation markers** is on the battlefield, if one or more of your units are within the same **terrain area** as that **operation marker**, and no enemy units are within that **terrain area**. | 5 | eob |

### Action: Sensor Sweep

| | |
|---|---|
| **STARTS** | Your Shooting phase. |
| **UNITS** | One friendly unit within range of one **central objective**. |
| **USE LIMIT** | Once per turn. |
| **COMPLETES** | End of your turn, if your unit controls that **objective**. |
| **EFFECT** | Your unit **performs a sensor sweep**: remove one **operation marker** from the battlefield. |
| **RESTRICTIONS** | A unit cannot start this action if there is only one **operation marker** on the battlefield. |

## Sabotage

**You:** Priority Assets · **Opponent:** Priority Assets · [card page](https://gdmissions.app/11th/primary-missions/priority-assets/sabotage) · [layouts](../layouts.md#priority-assets-mirror)

### ANY BATTLE ROUND

**When:** End of your turn

| Condition | VP | Flags |
|---|---|---|
| For each friendly unit that **committed sabotage** this turn (see reverse). | 3 | perUnit |
| For each of those units that is within range of one or more **objectives** in your opponent's territory. | 2 | cumulative, perUnit |

### SECOND BATTLE ROUND ONWARDS

**When:** End of your Command phase (or the end of your turn in the fifth battle round)

| Condition | VP | Flags |
|---|---|---|
| You control one or more **objectives** (excluding your **home objective**). | 4 |  |

### Action: Sabotage

| | |
|---|---|
| **STARTS** | Your Shooting phase. |
| **UNITS** | One unit within range of one **objective** (excluding your **home objective**). |
| **USE LIMIT** | Unlimited. Each unit that starts this **action** this phase must be within range of a different **objective**. |
| **COMPLETES** | End of your turn, if that unit controls that **objective**. |
| **EFFECT** | Your unit **commits sabotage**. |

## Secure Asset

**You:** Priority Assets · **Opponent:** Take and Hold · [card page](https://gdmissions.app/11th/primary-missions/priority-assets/secure-asset) · [layouts](../layouts.md#priority-assets-vs-take-and-hold)

### ANY BATTLE ROUND

**When:** End of your turn

| Condition | VP | Flags |
|---|---|---|
| A friendly unit **secured the asset** this turn (see reverse). | 4 |  |
| One or more enemy units that started the turn within range of one or more **central objectives** are **destroyed**. | 2 |  |

### SECOND BATTLE ROUND ONWARDS

**When:** End of your Command phase (or the end of your turn in the fifth battle round)

| Condition | VP | Flags |
|---|---|---|
| You control one or more **objectives** (excluding your **home objective**). | 4 |  |
| You control three or more **objectives**. | 4 |  |

### Action: Secure Asset

| | |
|---|---|
| **STARTS** | Your Shooting phase. |
| **UNITS** | One friendly unit within range of one **objective** (excluding your **home objective**). |
| **USE LIMIT** | Once per turn. |
| **COMPLETES** | End of your turn, if your unit controls that **objective**. |
| **EFFECT** | Your unit **secures the asset**. |

## Vanguard Operation

**You:** Priority Assets · **Opponent:** Reconnaissance · [card page](https://gdmissions.app/11th/primary-missions/priority-assets/vanguard-operation) · [layouts](../layouts.md#priority-assets-vs-reconnaissance)

### ANY BATTLE ROUND

**When:** End of your turn

| Condition | VP | Flags |
|---|---|---|
| A friendly unit **performed a vanguard operation** this turn. | 4 |  |
| One or more enemy units were **destroyed** this turn. | 2 |  |

### SECOND BATTLE ROUND ONWARDS

**When:** End of your Command phase (or the end of your turn in the fifth battle round)

| Condition | VP | Flags |
|---|---|---|
| You control one or more **objectives** (excluding your **home objective**). | 4 |  |

### END OF BATTLE

| Condition | VP | Flags |
|---|---|---|
| You control your opponent's **home objective**. | 10 | eob |

### Action: Vanguard Operation

| | |
|---|---|
| **STARTS** | Your Shooting phase. |
| **UNITS** | One friendly unit within one terrain area that is within your **opponent's territory**. |
| **USE LIMIT** | Once per turn. |
| **COMPLETES** | End of your turn, if no enemy units are within that **terrain area**. |
| **EFFECT** | Your unit **performs a vanguard operation**. |

## Vital Link

**You:** Priority Assets · **Opponent:** Purge the Foe · [card page](https://gdmissions.app/11th/primary-missions/priority-assets/vital-link) · [layouts](../layouts.md#priority-assets-vs-purge-the-foe)

### ANY BATTLE ROUND

**When:** End of your turn

| Condition | VP | Flags |
|---|---|---|
| You control one or more **central objectives**. | 2 |  |
| For each of your **operation markers** within range of one of those **objectives** (see reverse). | 1 | cumulative, perUnit |

### SECOND BATTLE ROUND ONWARDS

**When:** End of your Command phase (or the end of your turn in the fifth battle round)

| Condition | VP | Flags |
|---|---|---|
| You control one or more **objectives** (excluding your **home objective**). | 4 |  |
| One or more of those **objectives** is a **central objective**. | 4 | cumulative |

### END OF BATTLE

| Condition | VP | Flags |
|---|---|---|
| You control your opponent's **home objective**. | 10 | eob |

### Action: Maintain Control

| | |
|---|---|
| **STARTS** | Your Shooting phase. |
| **UNITS** | One friendly unit within range of one **central objective**. |
| **USE LIMIT** | Once per turn. |
| **COMPLETES** | End of your turn, if your unit controls that **objective**. |
| **EFFECT** | Place one of your **operation markers** within range of that **objective**. |
