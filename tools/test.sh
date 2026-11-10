#!/bin/bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build/native-tests build/native-box2d
for test in cat_idle_proxy_test cat_idle_gestures_test cat_random_zoomies_test cat_video_ground_bridge_test cat_coat_test cat_native_care_fixture_test cat_care_presentation_test cat_sprite_sequence_test cat_gallop_test cat_walk_dynamics_test cat_articulated_walk_test cat_recline_transition_test cat_locomotion_rig_test cat_pose_transition_test cat_cursor_animation_test cat_airborne_pose_test cat_belly_rig_test cat_species_belly_test cat_deformation_test cat_memory_motion_test cat_memory_test cat_memory_storage_test cat_routine_test cat_memory_lifetime_test cat_behavior_test cat_toy_test cat_render_geometry_test cat_render_placement_test cat_animation_test cat_nutrition_test cat_fish_test cat_preferences_test cat_update_notice_test pet_state_test update_protocol_test cat_environment_test cat_interaction_settings_test cat_life_test cat_expression_test; do
 g++ -std=c++17 -O2 -Wall -Wextra "tests/$test.cpp" -I src -o "build/native-tests/$test"
 "build/native-tests/$test"
done
python3 -m unittest discover -s tests -p '*_test.py'
for source in third_party/box2d/src/*.c; do
 gcc -std=c17 -D_POSIX_C_SOURCE=200809L -O2 -DNDEBUG -ffp-contract=off -mno-avx -mno-avx2 -Ithird_party/box2d/include -Ithird_party/box2d/src -c "$source" -o "build/native-box2d/$(basename "${source%.c}").o"
done
ar rcs build/native-box2d/libbox2d.a build/native-box2d/*.o
g++ -std=c++17 -O2 -Wall -Wextra tests/cat_physics_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_physics_test
build/native-tests/cat_physics_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_toy_physics_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_toy_physics_test
build/native-tests/cat_toy_physics_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_motion_gate_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_motion_gate_test
g++ -std=c++17 -O2 -Wall -Wextra tests/cat_ground_drive_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_ground_drive_test
build/native-tests/cat_ground_drive_test
build/native-tests/cat_motion_gate_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_cursor_session_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_cursor_session_test
build/native-tests/cat_cursor_session_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_ball_physics_test.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_ball_physics_test
build/native-tests/cat_ball_physics_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_companion_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_companion_test
build/native-tests/cat_companion_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_companion_pacing_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_companion_pacing_test
build/native-tests/cat_companion_pacing_test

g++ -std=c++17 -O2 -Wall -Wextra tests/cat_companion_zoomies_test.cpp src/cat_physics.cpp -Ithird_party/box2d/include build/native-box2d/libbox2d.a -lm -o build/native-tests/cat_companion_zoomies_test
build/native-tests/cat_companion_zoomies_test
