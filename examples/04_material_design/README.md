# Material Design 3 Example

This example showcases the Material Design 3 (Material You) components implemented in AromaUI.

## Features

### Smooth Antialiased Edges
All shapes are rendered with high-quality antialiasing using SDF (Signed Distance Field) techniques in the fragment shader. This provides smooth, crisp edges at any scale.

### Material Design 3 Components

#### Buttons
- **Filled Button** - Primary action with solid background color
- **Tonal Button** - Secondary action with tinted background  
- **Outlined Button** - Tertiary action with border only

#### Chips
- **Filter Chips** - Toggleable chips for filtering content
- Supports selected/unselected states
- Smooth state transitions

#### Cards
- **Elevated Card** - Card with shadow for depth (elevation level 1)
- **Filled Card** - Card with tinted background color
- **Outlined Card** - Card with border outline

#### FAB (Floating Action Button)
- Circular button that floats above content
- Primary action for the screen
- Available in small (40dp), normal (56dp), large (96dp), and extended sizes

### Material Design 3 Specifications

**Corner Radius:**
- Extra-small: 4dp (text fields)
- Small: 8dp (chips)
- Medium: 12dp (cards)
- Large: 16dp
- Extra-large: 28dp
- Full: 999dp / height/2 (buttons, FAB)

**Elevation:**
- Level 0: No shadow (outlined components)
- Level 1: Subtle shadow (elevated cards)
- Level 3: Medium shadow (FAB)

**Color System:**
- Primary: Purple (`#6750A4`)
- Surface: Warm white (`#FFFBFE`)
- Outline: Medium gray (`#79747E`)
- On Surface: Near black (`#1C1B1F`)

## Building

```bash
mkdir -p build
cd build
cmake ..
make
./material_design
```

## Interaction

- Click the **Filled Button** to increment the counter
- Click **Filter Chips** to toggle their selected state
- Click the **Outlined Button** to reset the counter
- Click the **FAB** (+) to add 10 to the counter

## Technical Implementation

The smooth antialiased edges are achieved through:
1. High-precision SDF functions in the fragment shader
2. Proper alpha blending with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`
3. Smoothstep function for smooth alpha transitions
4. AA_WIDTH constant for consistent edge quality

All colors follow the Material Design 3 color token system, ensuring a cohesive and modern appearance.
