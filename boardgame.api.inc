<?php
// $Id$
/**
 * @file
 * API functions to handle manipulation of player and game data.
 */
/**
 * Helper fuction for storing any errors
 *
 * @param $message
 *   If present, the provided message is stored in a static variable.
 * @return
 *   The previously stored message, or NULL if empty.
 */
function _boardgame_error($message = NULL) {
  static $error;
  if (isset($message)) {
    $error = $message;
  }
  return $error;
}
/**
 * Save the last error message.
 *
 * @param $message
 *    Error message to store
 */
function boardgame_set_error($message) {
  _boardgame_error($message);
}
/**
 * Retrieve the last error message.
 *
 * @return
 *    The last error message string.
 */
function boardgame_get_error() {
  return _boardgame_error();
}

/**
 * Get information on supported game types
 */
function boardgame_gametypes(){
  static $types = NULL;

  if (!isset($types)) {
    $types = module_invoke_all('boardgame_gametypes');
  }

  return $types;
}

/**
 * Initialize a board and store it to the database
 *
 * @param $playerid
 *   The player initializing this game.
 *
 * @param object $game_info
 *    A game initialization object.
 * e.g.
 * @code
 *  {
 *    type = 'chess',
 *    rows = 8,
 *    cols = 8,
 *    player_count = 2,
 *    pieces = [
 *      {
 *        'position' = 'a2',
 *        'type' = 'pawn',
 *        'set' = 'white'
 *      },
 *      {
 *        'position' = 'a7',
 *        'type' = 'pawn',
 *        'set' = 'black'
 *      }
 *    ]
 *  }
 * @endcode
 * The pieces array is identical to the move array required for
 * boardgame_update_game() and boardgame_board_apply_move(), with the exlusion of
 * the 'action' attribute (which is always 'add' during initialization).
 *
 * @return
 *    The id for the new game.
 */
function boardgame_init_game($playerid, $game_info) {
  $gametypes = boardgame_gametypes();

  if ($game_info->player_count < 2) {
    boardgame_set_error(t(
      'Unable to create game: player count must be 2 or greater.'
    ));
    return FALSE;
  }

  // Initialize board state.
  if (isset($game_info->pieces)) {
    // Normalize setup actions to objects and apply to board.
    $pieces = array();
    foreach ($game_info->pieces as $key => $action) {
      if (is_array($action)) {
        $pieces[$key] = (object) $action;
        $game_info->pieces[$key] = (object) $action;
      }
      elseif (is_object($action)) {
        $pieces[$key] = clone $action;
      }
      else{
        boardgame_set_error(t('Invalid setup action provided (index @key)', array('@key' => $key)));
        return FALSE;
      }

      // 'action' isn't specified for board setup, but is required in apply_move()
      $pieces[$key]->action = 'add';
    }
    $board = boardgame_board_apply_move(array(), $pieces);
    if (!$board) {
      // bubble up error message
      return FALSE;
    }
  }
  // Load default layout if available
  else if (isset($gametypes[$game_info->type]['initialize'])) {
    $func = $gametypes[$game_info->type]['initialize'];
    $board = $func();
  }
  else {
    $board = array();
  }

  // Create game entry, initialize board, and add starting player
  $result = db_query('INSERT INTO {boardgame_game}(type, player_count) VALUES("%s", %d)', array($game_info->type, $game_info->player_count));
  $gameid = db_last_insert_id('boardgame_game', 'gameid');

  db_query('INSERT INTO {boardgame_ply}(gameid, move, state, updated) VALUES(%d, "%s", "%s", %d)',
    array(
      $gameid,
      serialize($game_info->pieces),
      serialize($board),
      time(),
    )
  );
  boardgame_game_add_player($gameid, $playerid);

  return $gameid;
}

/**
 * Get info for the specified game, including game type and board state.
 *
 * @param $game
 *   The id of the game to fetch the board for
 * @param $ply
 *   (optional) The ply to fetch the game state at.  If not specified,
 *    the latest ply will be returned.
 *
 * @return
 *    An object representing the game and its state.
 *    e.g.
 * @code
 *    {
 *      gameid = 1,
 *      type = 'amazons',
 *      player_count = 2, # Number of players required to play the game
 *      current_ply = 4,  # The latest ply of the game
 *      ply = 2           # The ply this object represents
 *      state = array(),  # A keyed array of objects storing tile information
 *      move = array(),   # An array of move instructions for the given ply.
 *      move_time = 0,    # A UNIX timestamp of when the ply was stored.
 *      players = array() # An array of the player ids registed to the game.
 *    }
 * @endcode
 */
function boardgame_get_game($gameid, $ply = NULL) {
  $cache = array();
  $query_param = array($gameid);

  if (isset($ply)) {
    if (!empty($cache[$gameid][$ply])) {
      return $cache[$gameid][$ply];
    }
    $query_ply = '%d';
    array_unshift($query_param, $ply);
  }
  else { // retrieve the latest state of the board
    $query_ply = 'bgg.current_ply';
  }
  $result = db_query(
      'SELECT bgg.gameid, bgg.type, bgg.player_count, ' .
      'bgg.current_ply, bgp.ply, bgp.state, bgp.move, bgp.updated move_time ' .
      'FROM {boardgame_game} bgg INNER JOIN {boardgame_ply} bgp ON bgg.gameid = bgp.gameid AND bgp.ply = ' . $query_ply .
      ' WHERE bgg.gameid = %d',
      $query_param
    );
  $game = db_fetch_object($result);

  if (!$game) {
    boardgame_set_error(t(
      'Could not retrieve board for game #@game' . (isset($ply)?', ply #@ply':''),
      array('@game' => $gameid, '@ply' => $ply)
    ));
    return FALSE;
  }

  $game->players = boardgame_game_get_players($game->gameid);

  $game->state = unserialize($game->state);
  $game->move = unserialize($game->move);

  $cache[$gameid][$game->ply] = $game;

  return $game;
}

/**
 * Retrieve all games which have an available player slot, optionally
 * restricted to those a specific player is not already registered in.
 *
 * @param $playerid
 *    If specified, games which include this player will be removed from the
 *    returned list.
 *
 * @return
 *    An array of games.
 */
function boardgame_get_open_games($type = NULL, $playerid = NULL) {
  $query_where = array();
  $query_param = array();

  if(!empty($type)){
    $query_where[] = 'bgg.type = "%s"';
    $query_param[] = $type;
  }
  if (!empty($playerid)) {
    $query_where[] = '%d IN (SELECT playerid FROM {boardgame_game_player} bggp WHERE bggp.gameid = bgg.gameid)';
    $query_param[] = $playerid;
  }

  $result = db_query('SELECT gameid FROM {boardgame_game} bgg ' .
      'WHERE ' .
      'player_count > (SELECT count(playerid) FROM {boardgame_game_player} bggp WHERE bggp.gameid = bgg.gameid)' .
      (!empty($query_where)? ' AND ' . implode(' AND ', $query_where): ''),
      $query_param);

  $games = array();
  while ($game = db_fetch_object($result)) {
    $games[] = boardgame_get_game($game->gameid);
  }
  return $games;
}

/**
 * Retrieve all games which are in progress, optionally
 * restricted to those a specific player is registered in.
 *
 * In progress means the player quota has been reached and that the game
 * is not yet closed.
 *
 * @param $playerid
 *    If specified, only games which include this player will be returned.
 * @return
 *    An array of games.
 */
function boardgame_get_active_games($type = NULL, $playerid = NULL) {
  $query_where = array();
  $query_param = array();

  if(!empty($type)){
    $query_where[] = 'bgg.type = "%s"';
    $query_param[] = $type;
  }
  if (!empty($playerid)) {
    $query_where[] = '%d IN (SELECT playerid FROM {boardgame_game_player} bggp WHERE bggp.gameid = bgg.gameid)';
    $query_param[] = $playerid;
  }

  $result = db_query('SELECT gameid FROM {boardgame_game} bgg ' .
      'WHERE (current_ply > 0 OR ' .
      'player_count <= (SELECT count(playerid) FROM {boardgame_game_player} bggp WHERE bggp.gameid = bgg.gameid))' .
      (!empty($query_where)? ' AND ' . implode(' AND ', $query_where): ''),
      $query_param);

  $games = array();
  while ($game = db_fetch_object($result)) {
    $games[] = boardgame_get_game($game->gameid);
  }
  return $games;
}

/**
 * Retrieve all games not yet closed, optionally restricted to those
 * a specific player is registered in.
 *
 * @param $playerid
 * @return
 *    An array of games.
 */
function boardgame_get_games($type = NULL, $playerid = NULL) {
  $query_where = array();
  $query_param = array();

  if(!empty($type)){
    $query_where[] = 'bgg.type = "%s"';
    $query_param[] = $type;
  }
  if (!empty($playerid)) {
    $query_where[] = '%d IN (SELECT playerid FROM {boardgame_game_player} bggp WHERE bggp.gameid = bgg.gameid)';
    $query_param[] = $playerid;
  }

  $result = db_query('SELECT gameid FROM {boardgame_game} bgg' .
      (!empty($query_where)? ' WHERE ' . implode(' AND ', $query_where): ''),
      $query_param
    );

  $games = array();
  while ($game = db_fetch_object($result)) {
    $games[] = boardgame_get_game($game->gameid);
  }
  return $games;
}

/**
 * Apply the provided move to the specified game and store the new
 * state in the database.
 *
 * @param $gameid
 *    The ID of the game to update.
 * @param array $move
 *    An array of operations to perform for the move
 *    @see boardgame_board_apply_move()
 *
 * @return
 *    The number of the new ply on success, or FALSE on failure.
 */
function boardgame_update_game($gameid, $playerid, $move) {
  $last_state = boardgame_get_game($gameid);

  // normalize each action to an object
  foreach ($move as $key => $action) {
    if (is_array($action)) {
      $move[$key] = (object) $action;
    }
    elseif (!is_object($action)) {
      boardgame_set_error(t('Invalid move action provided (index @key)', array('@key' => $key)));
      return FALSE;
    }
  }

  // validate the move before applying
  $gametypes = boardgame_gametypes();
  if (isset($gametypes[$last_state->type]['validate_move'])) {
    $func = $gametypes[$last_state->type]['validate_move'];
    if (!$func($last_state->state, $move)) {
      return FALSE;
    }
  }
  $board = boardgame_board_apply_move($last_state->state, $move);
  if (!$board) {
    return FALSE;
  }

  $result = db_query('INSERT INTO {boardgame_ply}(gameid, playerid, ply, updated, move, state) VALUES(%d, %d, %d, %d, "%s", "%s")',
    array(
      $gameid,
      $playerid,
      ($last_state->ply + 1),
      time(),
      serialize($move),
      serialize($board)
    )
  );

  if (!$result) {
    boardgame_set_error(t(
      'Error storing move'
    ));
    return FALSE;
  }
  db_query('UPDATE {boardgame_game} SET current_ply = %d WHERE gameid = %d', array(($last_state->ply + 1), $gameid));

  return $last_state->ply + 1;
}

/**
 * Apply a move to the given board.
 *
 * @todo allow out-of order operations by only checking state validity after all operations are performed.
 *
 * @param array $board
 *    The array of board tiles to apply the move to.
 * @param array $move
 *    An array of atomic actions to apply to the board.
 *    Removal actions should be specified before additions in order to maintain
 *     a valid state.
 * @code
 *  [
 *    {
 *      'action' = 'remove',
 *      'position' = 'a2',
 *      'type' = 'pawn',
 *      'set' = 'white'
 *    },
 *    {
 *      'action' = 'add',
 *      'position' = 'a3',
 *      'type' = 'pawn',
 *      'set' = 'white'
 *    }
 *  ]
 * @endcode
 */
function boardgame_board_apply_move($board, $move) {

  foreach ($move as $action) {
    if (!preg_match('/[a-zA-Z0-9]+/', $action->position)) {
      boardgame_set_error(t(
        'Board position (@position) is not valid.',
        array('@position' => $action->position)
      ));
      return FALSE;
    }
    if ($action->action == 'add') {
      if (!empty($board[$action->position])) {
        boardgame_set_error(t(
          'Cannot add @set @type to position (@position): Tile already occupied.',
          array('@type' => $action->type, '@set' => $action->set, '@position' => $action->position)
        ));
        return FALSE;
        // TODO allow multiple units per tile
      }
      else{
        $board[$action->position]->type = $action->type;
        $board[$action->position]->set = $action->set;
      }
    }
    elseif ($action->action == 'remove') {
      if (empty($board[$action->position]) || $board[$action->position]->type != $action->type || $board[$action->position]->set != $action->set) {
        boardgame_set_error(t(
          'Cannot remove @set @type from position (@position): Not present in tile.',
          array('@type' => $action->type, '@set' => $action->set, '@position' => $action->position)
        ));
        return FALSE;
      }
      else{
        $board[$action->position] = NULL;
      }
    }
    else{
      boardgame_set_error(t('Invalid action "@action"', array('@action' => $action->action)));
      return FALSE;
    }
  }

  return $board;
}

/**
 * Add the specified player to the game.  If the player quota is then met,
 * generate priorities for all players.
 *
 * @param $gameid
 *    The game id to add the player to.
 * @param $playerid
 *    The player id to add to the game
 *
 * @return
 *    TRUE if the player was added succesfully, or FALSE on error.
 */
function boardgame_game_add_player($gameid, $playerid) {

  $game_info = boardgame_get_game($gameid);

  if (!$game_info) {
    boardgame_set_error(t(
      'Cannot add player: Invalid game id specified (@game)',
      array('@game' => $gameid)
    ));
    return FALSE;
  }
  // check if the game has started yet
  if ($game_info->current_ply) {
    boardgame_set_error(t(
      'Cannot add player @player to game @game: Game already in progress.',
      array('@player' => $playerid, '@game' => $gameid)
    ));
    return FALSE;
  }

  // query current players
  $players = boardgame_game_get_players($gameid);

  // check if player quota is already reached
  if (count($players) >= $game_info->player_count) {
    boardgame_set_error(t(
      'Cannot add player @player to game @game: Player quota reached.',
      array('@player' => $playerid, '@game' => $gameid)
    ));
    return FALSE;
  }

  if (!empty($players) && array_search($playerid, $players) !== FALSE) {
    boardgame_set_error(t(
      'Cannot add player @player to game @game: Player already added.',
      array('@player' => $playerid, '@game' => $gameid)
    ));
    return FALSE;
  }

  // add new player
  db_query('INSERT INTO {boardgame_game_player}(gameid, playerid) VALUES(%d, %d)', array($gameid, $playerid));

  // if quota met, generate priorities
  if (count($players)+1 == $game_info->player_count) {
    boardgame_game_set_player_sequence($gameid);
  }

  return TRUE;
}

/**
 * Retrieve an array of all players which are currently registered to
 * the specified game.
 */
function boardgame_game_get_players($gameid) {
  $result = db_query('SELECT playerid FROM {boardgame_game_player} WHERE gameid = %d ORDER BY sequence ASC', array($gameid));

  $players = array();
  while ($p = db_fetch_object($result)) {
    $players[] = $p->playerid;
  }

  return $players;
}

/**
 * Retrieve the registered players for the specified game, in sequence.
 *
 * @param $gameid
 *    The game to retrieve players for.
 * @return array
 *    An array of player ids, in sequence as determined at the start of the game.
 *    If the player quota is not met, FALSE is returned since the sequence has
 *     not been generated.
 */
function boardgame_game_get_player_sequence($gameid) {
  $game_info = boardgame_get_game($gameid, 0);
  $players = boardgame_game_get_players($gameid);

  if (count($players) < $game_info->player_count) {
    boargame_set_error(t(
      'Player priorities have not yet been determined: Player quota not yet reached.',
      array()
    ));
    return FALSE;
  }

  return $players;
}
/**
 * Generate the priority values of players for the specified game and store
 * them in the database.
 */
function boardgame_game_set_player_sequence($gameid) {
  $players = boardgame_game_get_players($gameid);

  // shuffle the array of players; new numeric keys are assigned
  shuffle($players);

  // store the priorities
  foreach ($players as $key => $playerid) {
    db_query(
      'UPDATE {boardgame_game_player} SET sequence = %d WHERE gameid = %d AND playerid = %d',
      array($key, $gameid, $playerid)
    );
  }
}

/**
 * Register a player in the database.
 * @param $info
 *    A keyed array of additional information for the player
 *    e.g.
 * @code
 *    [
 		'uid' => 1,
 *      'name' => 'bot',
 *      'description' => 'awesome'
 *    ]
 * @endcode
 */
function boardgame_player_create($info) {
  $success = drupal_write_record('boardgame_player', $info);

  return ($success)? $info['playerid'] : FALSE;
}
/**
 * Update a player's info in the database.
 *
 * @param $info
 *    a keyed array of the information to change.  If a key is not present,
 *    the current value will be kept
 *    e.g.
 * @code
 *    [
 *      'playerid' => 1,
 *      'name' => 'bot',
 *      'description' => 'awesome'
 *    ]
 * @endcode
 */
function boardgame_player_update($info) {
  $success = drupal_write_record('boardgame_player', $info, array('playerid'));

  return ($success && db_affected_rows())? TRUE : FALSE;
}

/**
 * Get the user who owns the specified player.
 *
 * @param $playerid
 *
 * @return
 *    The user id of the owner, or false if the playerid is invalid.
 */
function boardgame_player_get_user($playerid){
  $uid = db_result(db_query('SELECT uid FROM {boardgame_player} WHERE playerid = %d', array($playerid)));

  if (!$uid) {
    boardgame_set_error(t('Could not retrieve user of specified player.'));
  }

  return $uid;
}

/**
 * Retrieve all the players that the specified user owns.
 *
 * @param $uid
 *
 * @return
 *    An array of playerids.
 */
function boardgame_user_get_players($uid){
  $players = array();

  $result = db_query('SELECT playerid FROM {boardgame_player} WHERE uid = %d', array($uid));

  while ($p = db_fetch_object($result)) {
    $players[] = $p->playerid;
  }

  return $players;
}