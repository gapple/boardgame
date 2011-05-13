<?php
/**
 * @file
 * Integration functions for Services.module
 */

function _boardgame_game_resource_definition() {

  $resource['game']['create'] = array(
    'help' => 'Create a new game.',
    'file' => array('file' => 'inc', 'module' => 'boardgame', 'name' => 'services/game_resource'),
    'callback' => '_boardgame_game_resource_create',
    'access callback' => '_boardgame_game_resource_access',
    'access arguments' => array('create'),
    'access arguments append' => TRUE,
    'args' => array(
      array(
        'name' => 'playerid',
        'type' => 'int',
        'source' => array('data' => 'playerid'),
        'optional' => FALSE,
      ),
      array(
        'name' => 'boarddata',
        'type' => 'struct',
        'description' => 'Board initialization object.',
        'source' => array('data' => 'boarddata'),
        'optional' => FALSE,
      ),
    ),
  );

  $resource['game']['retrieve'] = array(
    'help' => 'Retrieve a game.',
    'file' => array('file' => 'inc', 'module' => 'boardgame', 'name' => 'services/game_resource'),
    'callback' => '_boardgame_game_resource_retrieve',
    'access callback' => '_boardgame_game_resource_access',
    'access arguments' => array('retrieve'),
    'access arguments append' => TRUE,
    'args' => array(
      array(
        'name' => 'gameid',
        'type' => 'int',
        'description' => 'Game Identifier',
        'source' => array('path' => 0),
        'optional' => FALSE,
      ),
      array(
        'name' => 'ply',
        'type' => 'int',
        'description' => 'Game Ply',
        'source' => array('params' => 'ply'),
        'optional' => TRUE,
      ),
    ),
  );

  $resource['game']['index'] = array(
    'help' => 'List games.',
    'file' => array('file' => 'inc', 'module' => 'boardgame', 'name' => 'services/game_resource'),
    'callback' => '_boardgame_game_resource_index',
    'access callback' => '_boardgame_game_resource_access',
    'access arguments' => array('index'),
    'access arguments append' => TRUE,
    'args' => array(
      array(
        'name' => 'parameters',
        'type' => 'array',
        'source' => 'params',
        'optional' => TRUE,
        'default value' => array(),
      ),
    ),
  );

  $resource['game']['targeted actions'] = array(

  );

  return $resource;
}

/**
 *
 */
function _boardgame_game_resource_access($op) {
  $access = FALSE;

  $args = func_get_args();
  array_shift($args); // remove $op

  switch ($op) {
    case 'create':
      // check that user is authorized to act for player
      $playerid = array_shift($args);
      module_load_include('inc', 'boardgame', 'boardgame.api');
      if (boardgame_player_get_user($playerid) == $user->uid) {
        $access = TRUE;
      }
      break;
    case 'retrieve':
      $access = user_access('view boardgame games');
      break;
    case 'index':
      $access = user_access('view boardgame games');
      break;
    default:
  }

  return $access;
}

function _boardgame_game_resource_create($playerid, $gamedata) {
  module_load_include('inc', 'boardgame', 'boardgame.api');

  global $user;
  if (boardgame_player_get_user($playerid) !== $user->uid) {
    return services_error(t('Not authorized to act for player @playerid', array('@playerid', $playerid)));
  }

  $gameid = boardgame_init_game($playerid, $gamedata);
  if (!$gameid) {
    return services_error(boardgame_get_error());
  }

  return (object) array(
    'gameid' => $gameid,
    'uri' => services_resource_uri(array('game', $gameid)),
  );
}

function _boardgame_game_resource_index($parameters) {
  module_load_include('inc', 'boardgame', 'boardgame.api');
  $result = array();
  $playerid = (!empty($parameters['player']) && is_numeric($parameters['player']))? intval($parameters['player']) : NULL;
  $type = !empty($parameters['type'])? $parameters['type'] : NULL;

  switch ($parameters['status']) {
    default:
    case 'all':
      $result = boardgame_get_games($type, $playerid);
      break;
    case 'active':
      $result = boardgame_get_active_games($type, $playerid);
      break;
    case 'open':
      $result = boardgame_get_open_games($type, $playerid);
      break;
  }

  return ($result !== FALSE)? $result : services_error(boardgame_get_error());
}

function _boardgame_game_resource_retrieve($gameid, $ply = NULL) {
  module_load_include('inc', 'boardgame', 'boardgame.api');
  return boardgame_get_game($gameid, $ply);
}