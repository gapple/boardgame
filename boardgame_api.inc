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
function _gameboard_error($message = NULL) {
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
function gameboard_set_error($message) {
  _gameboard_error($message);
}
/**
 * Retrieve the last error message.
 *
 * @return
 *    The last error message string.
 */
function gameboard_get_error() {
  return _gameboard_error();
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
 * boardgame_update_game() and boardgame_apply_move(), with the exlusion of
 * the 'action' attribute (which is always 'add' during initialization).
 *
 * @return
 *    The id for the new game.
 */
function boardgame_init_game($playerid, $game_info) {

  $board = array();
  // When incrementing strings containing only characters (/^([a-z]+|[A-Z]+)$/)
  //  PHP treats it similar to a number in base 26, maintaining case.
  //  e.g. 'a'++ = 'b', 'z'++ = 'aa', 'az'++ = 'ba'
  // See http://php.net/manual/en/language.operators.increment.php
  for ($col = 1, $col_alpha = 'a'; $col <= $game_info->cols; ++$col, ++$col_alpha) {
    for ($row = 1; $row <= $game_info->rows; ++$row) {
      $board[$col_alpha . $row] = NULL;
    }
  }

  if ($game_info->player_count < 2) {
    boardgame_set_error(t(
      'Unable to create game: player count must be 2 or greater.'
    ));
    return FALSE;
  }

  // apply positions
  // 'action' isn't specified for board setup, but is needed for application function
  foreach ($game_info->pieces as $key => $action) {
    $game_info->pieces[$key]['action'] = 'add';
  }
  $board = boardgame_apply_move($board, $game_info->pieces);

  // Create game entry, initialize board, and add starting player
  db_query('INSERT INTO {boardgame_game}(type, player_count) VALUES(%s, %d)', array($game_info->type, $game_info->player_count));
  $gameid = db_last_insert_id();

  db_query('INSERT INTO {boardgame_ply}(gameid, move, state) VALUES(%d, %s, %s)',
    array(
      $gameid,
      $game_info->pieces,
      $board
    )
   );
  boargame_game_player_add($gameid, $playerid);

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
 *  The board as an array of tiles, or FALSE if it could not be retrieved.
 */
function boardgame_get_game($gameid, $ply = NULL) {
  $cache = array();
  $query_param = array($gameid);

  if (isset($ply)) {
    if (!empty($cache[$gameid][$ply])) {
      return $cache[$gameid][$ply];
    }
    $query_ply = '%d';
    $query_param = array_shift($query_param, $ply);
  }
  else { // retrieve the latest state of the board
    $query_ply = 'bgg.current_ply';
  }

  $result = db_query(
      'SELECT bgg.gameid, bgg.type, bgg.player_count, bgp.ply, bgp.state, bgp.move ' .
      'FROM {boardgame_game} bgg LEFT JOIN {boardgame_ply} bgp ON bgg.gameid = bgp.gameid AND bpg.ply = ' . $query_ply .
      ' WHERE bgg.gameid = %d',
      $query_param
    );

  $state = db_fetch_object($result);

  if (!$state) {
    boardgame_set_error(t(
      'Could not retrieve board for game #@game' . (isset($ply)?', ply #@ply':''),
      array('@game' => $gameid, '@ply' => $ply)
    ));
    return FALSE;
  }

  $state->state = unserialize($state->state);
  $state->move = unserialize($state->move);

  $cache[$gameid][$state->ply] = $state;

  return $state;
}

/**
 * Apply the provided move to the specified game and store the new
 * state in the database.
 *
 * @param $gameid
 *    The ID of the game to update.
 * @param array $move
 *    An array of operations to perform for the move
 *    @see boardgame_apply_move()
 *
 * @return
 *    The number of the new ply on success, or FALSE on failure.
 */
function boardgame_update_game($gameid, $move) {
  $last_ply = boardgame_get_game($gameid);

  $board = boardgame_apply_move($last_ply->state, $move);
  if (!$board) {
    return FALSE;
  }

  db_query('INSERT INTO {boardgame_ply}(gameid, ply, move, state) VALUES(%d, %d, %s, %s)',
    array(
      $gameid,
      ($last_ply['ply'] + 1),
      $move,
      $board
    )
  );

  db_query('UPDATE {boardgame_game} SET current_ply = %d WHERE gameid = %d', array(($last_ply['ply'] + 1), $gameid));

  return $last_ply['ply'] + 1;
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
function boardgame_apply_move($board, $move) {

  foreach ($move as $action) {
    if (!array_key_exists($action->position, $board)) {
      boardgame_set_error(t(
        'Board position (@position) does not exist.',
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
        $board[$action->position] = array(
          'type' => $action->type,
          'player' => $action->player,
        );
      }
    }
    elseif ($action->action == 'remove') {
      if (empty($board[$action->position]) || $board[$action->position]->type != $action->type || $board[$action->position]->player != $action->player) {
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
function boargame_game_add_player($gameid, $playerid) {

  $game_info = boardgame_get_game($gameid);
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

  if (array_search($playerid, $players) !== FALSE) {
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
    $players[] = $p->player;
  }
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

