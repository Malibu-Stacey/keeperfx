/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file thing_corpses.c
 *     Dead creature things support functions.
 * @par Purpose:
 *     Functions to create and operate on dead creature corpses.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 Mar 2009 - 02 Mar 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "thing_corpses.h"

#include "globals.h"
#include "bflib_basics.h"

#include "thing_data.h"
#include "thing_stats.h"
#include "thing_list.h"
#include "thing_physics.h"
#include "thing_effects.h"
#include "thing_navigate.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_graphics.h"
#include "dungeon_data.h"
#include "config_creature.h"
#include "gui_topmsg.h"
#include "gui_soundmsgs.h"

#include "keeperfx.hpp"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
DLLIMPORT long _DK_update_dead_creature(struct Thing *thing);
DLLIMPORT struct Thing *_DK_create_dead_creature(struct Coord3d *pos, unsigned short model, unsigned short a1, unsigned short owner, long explevel);
DLLIMPORT struct Thing *_DK_destroy_creature_and_create_corpse(struct Thing *thing, long a1);
/******************************************************************************/


/******************************************************************************/
/**
 *  Returns if given corpse can rot in graveyard.
 * @param thing The dead creature thing.
 * @return True if the corpse can be dragged into graveyard for rotting.
 */
TbBool corpse_is_rottable(const struct Thing *thing)
{
    struct PlayerInfo *player;
    if (!thing_exists(thing))
        return false;
    if (thing->class_id != TCls_DeadCreature)
        return false;
    if ((get_creature_model_flags(thing) & MF_NoCorpseRotting) != 0)
        return false;
    player = get_player_thing_is_controlled_by(thing);
    if (player_invalid(player))
        return true;
    return false;
}

TbBool create_vampire_in_room(struct Room *room)
{
    struct Dungeon *dungeon;
    struct Thing *thing;
    struct Coord3d pos;
    long crmodel;
    pos.x.val = 0;
    pos.y.val = 0;
    pos.z.val = 0;
    crmodel = get_room_create_creature_model(room->kind);
    thing = create_creature(&pos, crmodel, room->owner);
    if (thing_is_invalid(thing)) {
        ERRORLOG("Could not create creature model %ld",crmodel);
        return false;
    }
    if (!find_random_valid_position_for_thing_in_room(thing, room, &pos)) {
        ERRORLOG("Could not find valid position in room");
        delete_thing_structure(thing, 0);
        return false;
    }
    move_thing_in_map(thing, &pos);
    dungeon = get_dungeon(room->owner);
    dungeon->lvstats.vamps_created++;
    create_effect(&pos, TngEff_Unknown03, thing->owner);
    if (is_my_player_number(room->owner)) {
        output_message(SMsg_GraveyardMadeVampire, 0, true);
    }
    return true;
}

unsigned int get_creature_blocked_flags_at(struct Thing *thing, struct Coord3d *newpos)
{
    struct Coord3d pos;
    unsigned int flags;
    flags = 0;
    pos.x.val = newpos->x.val;
    pos.y.val = thing->mappos.y.val;
    pos.z.val = thing->mappos.z.val;
    if ( creature_cannot_move_directly_to(thing, &pos) ) {
        flags |= 0x01;
    }
    pos.x.val = thing->mappos.x.val;
    pos.y.val = newpos->y.val;
    pos.z.val = thing->mappos.z.val;
    if ( creature_cannot_move_directly_to(thing, &pos) ) {
        flags |= 0x02;
    }
    pos.x.val = thing->mappos.x.val;
    pos.y.val = thing->mappos.y.val;
    pos.z.val = newpos->z.val;
    if ( creature_cannot_move_directly_to(thing, &pos) ) {
        flags |= 0x04;
    }
    switch (flags)
    {
    case 0:
      if ( creature_cannot_move_directly_to(thing, newpos) ) {
          flags = 0x07;
      }
      break;
    case 1:
      pos.x.val = thing->mappos.x.val;
      pos.y.val = newpos->y.val;
      pos.z.val = newpos->z.val;
      if (creature_cannot_move_directly_to(thing, &pos) < 1) {
          flags = 0x01;
      } else {
          flags = 0x07;
      }
      break;
    case 2:
      pos.x.val = newpos->x.val;
      pos.y.val = thing->mappos.y.val;
      pos.z.val = newpos->z.val;
      if (creature_cannot_move_directly_to(thing, &pos) < 1) {
          flags = 0x02;
      } else {
          flags = 0x07;
      }
      break;
    case 4:
      pos.x.val = newpos->x.val;
      pos.y.val = newpos->y.val;
      pos.z.val = thing->mappos.z.val;
      if ( creature_cannot_move_directly_to(thing, &pos) ) {
          flags = 0x07;
      }
      break;
    }
    return flags;
}

void remove_body_from_graveyard(struct Thing *thing)
{
    struct Dungeon *dungeon;
    struct Room *room;
    room = get_room_thing_is_on(thing);
    if (room_is_invalid(room)) {
        ERRORLOG("The %s is not in room",thing_model_name(thing));
        return;
    }
    if (room->kind != RoK_GRAVEYARD) {
        ERRORLOG("The %s is in %s instead of graveyard",thing_model_name(thing),room_code_name(room->kind));
        return;
    }
    if (room->used_capacity <= 0) {
        ERRORLOG("Graveyard had no allocated capacity to remove body from");
        return;
    }
    if (thing->byte_14 == 0) {
        ERRORLOG("The %s is not in graveyard",thing_model_name(thing));
        return;
    }
    room->used_capacity--;
    thing->byte_14 = 0;
    dungeon = get_dungeon(room->owner);
    dungeon->bodies_rotten_for_vampire++;
    dungeon->lvstats.graveyard_bodys++;
    if (dungeon->bodies_rotten_for_vampire >= game.bodies_for_vampire) {
        dungeon->bodies_rotten_for_vampire -= game.bodies_for_vampire;
        create_vampire_in_room(room);
    }
}

long move_dead_creature(struct Thing *thing)
{
    struct Coord3d pos;
    long i;
    if ( (thing->velocity.x.val != 0) || (thing->velocity.y.val != 0) || (thing->velocity.z.val != 0) )
    {
        i = (long)thing->mappos.x.val + (long)thing->velocity.x.val;
        if (i >= subtile_coord(map_subtiles_x,0)) i = subtile_coord(map_subtiles_x,0)-1;
        if (i < 0) i = 0;
        pos.x.val = i;
        i = (long)thing->mappos.y.val + (long)thing->velocity.y.val;
        if (i >= subtile_coord(map_subtiles_y,0)) i = subtile_coord(map_subtiles_y,0)-1;
        if (i < 0) i = 0;
        pos.y.val = i;
        i = (long)thing->mappos.z.val + (long)thing->velocity.z.val;
        if (i < 0) i = 0;
        pos.z.val = i;
        if ( !positions_equivalent(&thing->mappos, &pos) )
        {
          if ( thing_in_wall_at(thing, &pos) )
          {
              i = get_creature_blocked_flags_at(thing, &pos);
              slide_thing_against_wall_at(thing, &pos, i);
              remove_relevant_forces_from_thing_after_slide(thing, &pos, i);
          }
        }
        move_thing_in_map(thing, &pos);
    } else
    {
        // Even if no velocity, update field_60
        thing->field_60 = get_thing_height_at(thing, &thing->mappos);
    }
    return 1;
}

long update_dead_creature(struct Thing *thing)
{
    struct Coord3d pos;
    struct Map *mapblk;
    long i;
    SYNCDBG(18,"Starting");
    return _DK_update_dead_creature(thing);
    if ((thing->field_0 & 0x80) == 0)
    {
      if (thing->active_state == 1)
      {
        pos.x.val = thing->mappos.x.val;
        pos.y.val = thing->mappos.y.val;
        pos.z.val = thing->mappos.z.val;
        pos.z.val += 3 * (int)thing->field_58 / 4;
        if (creature_model_bleeds(thing->model)) {
            create_effect(&pos, TngEff_Unknown65, thing->owner);
        }
        if (thing->health > 0)
            thing->health--;
        if (thing->health <= 0) {
          thing->active_state = 2;
          i = get_creature_anim(thing, 16);
          set_thing_draw(thing, i, 64, -1, 1, 0, 2);
        }
      } else
      if ( corpse_is_rottable(thing) )
      {
          if (thing->byte_14 != 0)
          {
              if (thing->health > 0)
                  thing->health--;
              if (thing->health <= 0) {
                  remove_body_from_graveyard(thing);
                  delete_thing_structure(thing, 0);
                  return 0;
              }
          } else
          {
              if (game.play_gameturn - thing->field_9 > game.body_remains_for) {
                  delete_thing_structure(thing, 0);
                  return 0;
              }
          }
      }
    }
    i = get_top_cube_at(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    if (cube_is_water(i)) {
        thing->movement_flags |= TMvF_Unknown01;
    }
    if ((thing->field_0 & 0x20) != 0)
    {
        move_dead_creature(thing);
        return 1;
    }
    if ( map_pos_is_lava(thing->mappos.x.stl.num, thing->mappos.y.stl.num)
      && ((thing->field_1 & TF1_Unkn01) == 0) && ((thing->field_0 & 0x80) == 0) )
    {
        delete_thing_structure(thing, 0);
        return 0;
    }
    mapblk = get_map_block_at(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    if ((mapblk->flags & 0x40) == 0)
    {
        move_dead_creature(thing);
        return 1;
    } else
    {
        delete_thing_structure(thing, 0);
        create_dead_creature(&thing->mappos, thing->model, 2, thing->owner, thing->byte_13);
        return 0;
    }
}

TbBool update_dead_creatures_list(struct Dungeon *dungeon, struct Thing *thing)
{
  struct CreatureStorage *cstore;
  struct CreatureControl *cctrl;
  long i;
  SYNCDBG(18,"Starting");
  cctrl = creature_control_get_from_thing(thing);
  if ((dungeon == NULL) || creature_control_invalid(cctrl))
  {
    WARNLOG("Invalid victim or dungeon");
    return false;
  }
  // Check if the creature of same type is in list
  i = dungeon->dead_creatures_count-1;
  while (i >= 0)
  {
    cstore = &dungeon->dead_creatures[i];
    if ((cstore->model == thing->model) && (cstore->explevel == cctrl->explevel))
    {
      // This creature is already in list
      SYNCDBG(18,"Already in list");
      return false;
    }
    i--;
  }
  // Find a slot for the new creature
  if (dungeon->dead_creatures_count < DEAD_CREATURES_MAX_COUNT)
  {
    i = dungeon->dead_creatures_count;
    dungeon->dead_creatures_count++;
  } else
  {
    i = dungeon->dead_creature_idx;
    dungeon->dead_creature_idx++;
    if (dungeon->dead_creature_idx >= DEAD_CREATURES_MAX_COUNT)
      dungeon->dead_creature_idx = 0;
  }
  cstore = &dungeon->dead_creatures[i];
  cstore->model = thing->model;
  cstore->explevel = cctrl->explevel;
  SYNCDBG(19,"Finished");
  return true;
}

struct Thing *create_dead_creature(struct Coord3d *pos, unsigned short model, unsigned short a1, unsigned short owner, long explevel)
{
    struct Thing *thing;
    unsigned long k;
    //return _DK_create_dead_creature(pos, model, a1, owner, explevel);
    if (!i_can_allocate_free_thing_structure(TAF_FreeEffectIfNoSlots))
    {
        ERRORDBG(3,"Cannot create dead creature model %d for player %d. There are too many things allocated.",(int)model,(int)owner);
        erstat_inc(ESE_NoFreeThings);
        return INVALID_THING;
    }
    thing = allocate_free_thing_structure(TAF_FreeEffectIfNoSlots);
    if (thing->index == 0) {
        ERRORDBG(3,"Should be able to allocate dead creature %d for player %d, but failed.",(int)model,(int)owner);
        erstat_inc(ESE_NoFreeThings);
        return INVALID_THING;
    }
    thing->class_id = 4;
    thing->model = model;
    thing->parent_thing_idx = thing->index;
    thing->owner = owner;
    thing->byte_13 = explevel;
    thing->mappos.x.val = pos->x.val;
    thing->mappos.y.val = pos->y.val;
    thing->mappos.z.val = 0;
    thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos);
    thing->sizexy = 0;
    thing->field_58 = 0;
    thing->field_5A = 0;
    thing->field_5C = 0;
    thing->field_20 = 16;
    thing->field_23 = 204;
    thing->field_24 = 51;
    thing->field_22 = 0;
    thing->movement_flags |= TMvF_Unknown08;
    thing->field_9 = game.play_gameturn;
    if (creatures[model].field_7)
      thing->field_4F |= 0x30;
    add_thing_to_its_class_list(thing);
    place_thing_in_mapwho(thing);
    switch (a1)
    {
    case 2:
        thing->active_state = CrSt_ImpArrivesAtDigOrMine1;
        k = get_creature_anim(thing, 17);
        set_thing_draw(thing, k, 256, 300, 0, 0, 2);
        break;
    default:
        thing->active_state = CrSt_ImpDoingNothing;
        k = get_creature_anim(thing, 15);
        set_thing_draw(thing, k, 128, 300, 0, 0, 2);
        thing->health = 3 * get_lifespan_of_animation(thing->field_44, thing->field_3E);
        play_creature_sound(thing, 9, 3, 0);
        break;
    }
    thing->field_46 = (300 * (long)thing->byte_13) / 20 + 300;
    return thing;
}

struct Thing *destroy_creature_and_create_corpse(struct Thing *thing, long a1)
{
    struct CreatureControl *cctrl;
    struct PlayerInfo *player;
    struct Thing *deadtng;
    struct Coord3d pos;
    TbBool memf1;
    long owner;
    long crmodel;
    long explevel;
    long prev_idx;

    //return _DK_destroy_creature_and_create_corpse(thing, a1);
    crmodel = thing->model;
    memf1 = ((thing->field_0 & 0x20) != 0);
    pos.x.val = thing->mappos.x.val;
    pos.y.val = thing->mappos.y.val;
    pos.z.val = thing->mappos.z.val;
    owner = thing->owner;
    prev_idx = thing->index;
    cctrl = creature_control_get_from_thing(thing);
    explevel = cctrl->explevel;
    player = NULL;
    remove_creature_score_from_owner(thing);
    delete_thing_structure(thing, 0);
    deadtng = create_dead_creature(&pos, crmodel, a1, owner, explevel);
    if (thing_is_invalid(deadtng))
    {
        ERRORLOG("Could not create dead thing.");
        return INVALID_THING;
    }
    set_flag_byte(&deadtng->field_0, 0x20, memf1);
    if (owner != game.neutral_player_num)
    {
        // Update thing index inside player struct
        player = get_player(owner);
        if (player->controlled_thing_idx == prev_idx)
        {
            player->controlled_thing_idx = deadtng->index;
            player->field_31 = deadtng->field_9;
        }
    }
    return deadtng;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
