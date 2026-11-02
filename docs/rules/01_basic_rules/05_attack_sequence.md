# 05. ATTACK SEQUENCE

> Source: 11th Edition Core Rules PDF, pages 18-23. Verbatim pdftotext extraction; `<!-- page N -->` marks page breaks. Sidebar/diagram text may appear out of order.

<!-- page 18 -->

ATTACK SEQUENCE

05
18

Whenever models make attacks, you will follow the sequence detailed in this section to find out if they inflict damage.

WHEN DOES THIS SEQUENCE END? If an attack fails or inflicts damage, this sequence ends for that attack. When all attacks have either failed or inflicted damage, this sequence ends and those attacks have been resolved.

Each time the active player is instructed to resolve the attack sequence, they follow the steps below. In each step, if there is more than one dice to roll, make all of those rolls simultaneously.
1. HIT ROLLS 2. WOUND ROLLS 3. SAVE ROLLS 4. INFLICT DAMAGE

CRITICAL HITS AND CRITICAL WOUNDS
Critical hits are still hits, and critical wounds are still wounds. In addition,
other rules can
be triggered by
a critical hit or a critical wound, such as [LETHAL HITS] and [DEVASTATING WOUNDS] (24).

## 05.01 1. HIT ROLLS
Make one hit roll for each attack dice by rolling one D6. For each result, check if it fails or is a hit by matching the first condition below that applies:

<!-- Table reconstructed from the PDF page image: dice faces are icons that pdftotext cannot extract. -->
| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | FAILS |
| Unmodified 6 | CRITICAL HIT |
| Equal to or greater than that attack's BS/WS characteristic | HIT |
| Any other result | FAILS |

## 05.02 2. WOUND ROLLS
Make one wound roll for each hit by rolling one D6. For each result, check if it fails or is a wound by matching the first condition below that applies:

<!-- Table reconstructed from the PDF page image: dice faces are icons that pdftotext cannot extract. -->
| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | FAILS |
| Unmodified 6 | CRITICAL WOUND |
| Equal to or greater than the required result below | WOUND |
| Any other result | FAILS |

| ATTACK'S STRENGTH VS TARGET'S TOUGHNESS | REQUIRED RESULT |
|---|---|
| Strength is TWICE (or more than twice) the Toughness | 2+ |
| Strength is GREATER than the Toughness | 3+ |
| Strength is EQUAL to the Toughness | 4+ |
| Strength is LESS than the Toughness | 5+ |
| Strength is HALF (or less than half) the Toughness | 6+ |

++ A WISE MAN LEARNS FROM THE DEATHS OF OTHERS ++

<!-- page 19 -->

## 05.03 3. SAVE ROLLS
The opposing player resolves the following sequence:
1. Create Groups: Divide all models in the target unit into the following groups, as many times as required: 
- One group for each CHARACTER model. 
- One group for all other models with the same W, Sv and InSv characteristics.
2. Allocation Order: Declare the order in which those groups will have attacks allocated to them, applying all of the following: 
- If a non-CHARACTER group contains a model that has lost one or more wounds, that group must be first in the allocation order. 
- No CHARACTER group can be earlier in the allocation order than a non-CHARACTER group. 
- CHARACTER groups containing a model that has lost one or more wounds must be earlier in the allocation order than CHARACTER groups containing no wounded models.
3. Make Save Rolls: The opposing player makes one save roll for each attack that wounded the target by rolling one D6.

SEE ALSO

- Destroyed 
- Modified Characteristics 
- Modifying Damage 
- Modifying Dice Rolls 
- Random Characteristics

## 05.04 4. INFLICT DAMAGE
The opposing player resolves the following sequence for each save roll, working from lowest result(s) to highest result(s), until all attacks are resolved or all models in the target unit are destroyed – in the latter case, any excess attacks are lost.
1. Select Model: Select one model in the current allocation group (see right); this must be a model that has lost one or more wounds if possible.
2. Check Save Roll: For each result, check if that attack inflicts damage or fails by matching the first condition below that applies:

CURRENT ALLOCATION GROUP The first group in the allocation order begins as the current group. Once all models in an allocation group are destroyed, the next group in the allocation order becomes the current one.

<!-- Table reconstructed from the PDF page image: dice faces are icons that pdftotext cannot extract. -->
| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | INFLICTS DAMAGE |
| Invulnerable Save: The models in the current allocation group have an InSv characteristic, and the result is equal to or greater than that characteristic. | FAILS |
| Save and AP: After modifying the result by the attacking weapon's AP characteristic, it is equal to or greater than the Sv characteristic of models in the current allocation group. | FAILS |
| Any other result | INFLICTS DAMAGE |

Example: An AP characteristic of -1 would modify a save roll of 3 to a 2. For models with a Sv characteristic of 2+ or better, that attack would fail.

3. Resolve Damage: If that attack inflicts damage, the selected model loses a number of wounds equal to that attack’s D characteristic. If this reduces that model’s remaining wounds to 0 or fewer, it is destroyed.

19

<!-- page 20 -->

ATTACK SEQUENCE EXAMPLES

MAKING ATTACKS
BP
X3
HB BP X5 B
B

05
20

1. SELECT WEAPONS
The RED unit is attacking. The following weapons are selected to make attacks with:

- 2 boltguns (B) 
- 2 bolt pistols (BP) 
- 1 heavy bolter (HB)
2. SELECT TARGETS
The BLUE unit is selected as the target. The unit is visible to all models in the attacking unit. All of the selected weapons are in range, with the exception of one bolt pistol. As a result, that weapon will not make any attacks.
3. RESOLVE ATTACKS
There is only one enemy unit being targeted, so the controlling player now gathers attack dice:

- Five attack dice are gathered for the boltguns and bolt pistol, which have A characteristics of 2 and 1 respectively and all make identical attacks.

- Three attack dice are gathered for the heavy bolter, which has an A characteristic of 3 but does not make identical attacks.

<!-- page 21 -->

RESOLVING ATTACK DICE

BP X5 B

B

1.

2.

RESOLVING OTHER ATTACKS
X3
HB
1. 2.

3.
1. HIT ROLLS The controlling player chooses to make the five hit rolls for the boltguns and bolt pistol first. The BS characteristic of the weapons is 3+. Four of the attacks hit the target.
2. WOUND ROLLS The controlling player makes four wound rolls. The weapons have an S characteristic of 4 and the target unit has a T characteristic of 3, so rolls of 3+ are required to wound. Three of the attacks wound the target.
3. SAVE ROLLS The target unit’s controlling player makes three save rolls.
4. INFLICT DAMAGE 
- The lowest result is less than both the InSv and Sv
characteristics of the target, so that attack inflicts damage. This reduces the model to which that attack was allocated to 0 wounds, which destroys it. 
- The next lowest result is less than the target’s InSv characteristic, but greater than its Sv characteristic of 3+; that attack fails. 
- The other result is greater than the target’s InSv characteristic of 5+; that attack also fails.

3.

-1 to Save Roll

1. HIT ROLLS
The controlling player then makes three hit rolls for the heavy bolter. The BS characteristic of the weapon is 4+. Two of the attacks hit the target.
2. WOUND ROLLS
The controlling player makes two wound rolls. The weapon has an S characteristic of 5, so rolls of 3+ are required to wound. Both attacks wound the target.
3. SAVE ROLLS
The target unit’s controlling player makes two save rolls.
4. INFLICT DAMAGE 
- The lowest result, when modified by the attacking
weapon’s AP characteristic of -1, is less than the target’s Sv characteristic of 3+, so that attack inflicts damage. This reduces the model to which that attack was allocated to 0 wounds, which destroys it.

- The other result is equal to the target’s InSv characteristic of 5+; that attack fails.

++ BELLIGERENCE ALLIED TO FAITH CAN MOVE A MOUNTAIN ++

21

<!-- page 22 -->

ATTACK SEQUENCE EXAMPLES

ATTACKING ATTACHED UNITS
B B
B B B
B
X14

X1
B PP
HB HB
X6

05
22

1. SELECT WEAPONS
The RED unit is attacking. The following weapons are selected to make attacks with:

- 7 boltguns (B) 
- 1 plasma pistol (PP) 
- 2 heavy bolters (HB)
2. SELECT TARGETS
The BLUE unit is selected as the target. It is an attached unit (19) formed from a Seraphim unit and Saint Celestine (with her two Geminae Superia). The unit is visible to all models in the attacking unit, and all of the selected weapons are in range.

3. RESOLVE ATTACKS
There is only one enemy unit being targeted, so the controlling player now gathers attack dice. They decide to resolve the heavy bolter attacks first, which each have an A characteristic of 3, so six attack dice are gathered.
The attack dice for the remaining weapons will be gathered once the heavy bolter attacks are resolved (see opposite), as follows:

- 14 attack dice for the boltguns, which each have an A characteristic of 2.

- One attack dice for the plasma pistol, which has an A characteristic of 1.

<!-- page 23 -->

ALLOCATION GROUPS

HB HB

Save Rolls

2 2
2

2 2

1 31

1. CREATE GROUPS AND DECLARE ORDER
The target unit’s controlling player divides it into groups: one containing Saint Celestine, one containing the Geminae Superia, and one containing the Seraphim. They then declare the allocation order, choosing the Geminae Superia first (1), hoping their better Sv and InSv characteristics will weather the attacks. The Seraphim must be chosen second (2), as Saint Celestine is a CHARACTER model so must be last in the order (3).
2. RESOLVE ATTACK DICE
The heavy bolters’ attacks wound the target five times, so the target unit’s controlling player makes five save rolls.

The attacks are resolved one at a time, from lowest save rolls to highest:

- The two results of 1 are allocated first, to the current allocation group (the Geminae Superia). They both inflict damage, and both Geminae Superia are destroyed.

- The result of 3 is now allocated to the Seraphim, who have become the current allocation group. When modified by the attacking weapon’s AP characteristic of -1, this also inflicts damage, destroying one Seraphim model.

- The remaining attacks fail, so no further damage is inflicted.
3. SELECT NEXT GROUP OF ATTACK DICE AND REPEAT

23

