/*
 * mlt_playlist.c -- playlist service class
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "mlt_playlist.h"
#include "mlt_frame.h"

#include <stdio.h>
#include <stdlib.h>

/** Virtual playlist entry.
*/

typedef struct
{
	mlt_producer producer;
	mlt_timecode in;
	mlt_timecode playtime;
}
playlist_entry;

/** Private definition.
*/

struct mlt_playlist_s
{
	struct mlt_producer_s parent;
	struct mlt_producer_s blank;

	int size;
	int count;
	playlist_entry **list;
};

/** Forward declarations
*/

static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index );

/** Constructor.
*/

mlt_playlist mlt_playlist_init( )
{
	mlt_playlist this = calloc( sizeof( struct mlt_playlist_s ), 1 );
	if ( this != NULL )
	{
		mlt_producer producer = &this->parent;

		// Construct the producer
		mlt_producer_init( producer, this );

		// Override the producer get_frame
		producer->get_frame = producer_get_frame;

		// Initialise blank
		mlt_producer_init( &this->blank, NULL );

		// Indicate that this producer is a playlist
		mlt_properties_set_data( mlt_playlist_properties( this ), "playlist", this, 0, NULL, NULL );
	}
	
	return this;
}

/** Get the producer associated to this playlist.
*/

mlt_producer mlt_playlist_producer( mlt_playlist this )
{
	return &this->parent;
}

/** Get the service associated to this playlist.
*/

mlt_service mlt_playlist_service( mlt_playlist this )
{
	return mlt_producer_service( &this->parent );
}

/** Get the propertues associated to this playlist.
*/

mlt_properties mlt_playlist_properties( mlt_playlist this )
{
	return mlt_producer_properties( &this->parent );
}

/** Append to the virtual playlist.
*/

static int mlt_playlist_virtual_append( mlt_playlist this, mlt_producer producer, mlt_timecode in, mlt_timecode out )
{
	// Get the fps of the first producer
	double fps = mlt_properties_get_double( mlt_playlist_properties( this ), "first_fps" );

	mlt_timecode playtime = mlt_producer_get_playtime( mlt_playlist_producer( this ) ) + out - in;

	// If fps is 0
	if ( fps == 0 )
	{
		// Inherit it from the producer
		fps = mlt_producer_get_fps( producer );
	}
	else if ( fps != mlt_properties_get_double( mlt_producer_properties( producer ), "fps" ) )
	{
		// Generate a warning for now - the following attempt to fix may fail
		fprintf( stderr, "Warning: fps mismatch on playlist producer %d\n", this->count );

		// It should be safe to impose fps on an image producer, but not necessarily safe for video
		mlt_properties_set_double( mlt_producer_properties( producer ), "fps", fps );
	}

	// Check that we have room
	if ( this->count >= this->size )
	{
		int i;
		this->list = realloc( this->list, ( this->size + 10 ) * sizeof( playlist_entry * ) );
		for ( i = this->size; i < this->size + 10; i ++ )
			this->list[ i ] = NULL;
		this->size += 10;
	}

	this->list[ this->count ] = calloc( sizeof( playlist_entry ), 1 );
	this->list[ this->count ]->producer = producer;
	this->list[ this->count ]->in = in;
	this->list[ this->count ]->playtime = out - in;

	this->count ++;

	mlt_properties_set_double( mlt_playlist_properties( this ), "first_fps", fps );
	mlt_properties_set_double( mlt_playlist_properties( this ), "fps", fps );
	mlt_properties_set_timecode( mlt_playlist_properties( this ), "length", playtime );
	mlt_properties_set_timecode( mlt_playlist_properties( this ), "out", playtime );

	return 0;
}

/** Seek in the virtual playlist.
*/

static mlt_producer mlt_playlist_virtual_seek( mlt_playlist this )
{
	// Default producer to blank
	mlt_producer producer = &this->blank;

	// Map playlist position to real producer in virtual playlist
	mlt_timecode position = mlt_producer_position( &this->parent );

	// Loop through the virtual playlist
	int i = 0;

	for ( i = 0; i < this->count; i ++ )
	{
		if ( position < this->list[ i ]->playtime )
		{
			// Found it, now break
			producer = this->list[ i ]->producer;
			position += this->list[ i ]->in;
			break;
		}
		else
		{
			// Decrement position by length of this entry
			position -= this->list[ i ]->playtime;
		}
	}

	// Seek in real producer to relative position
	mlt_producer_seek( producer, position );

	return producer;
}

static mlt_producer mlt_playlist_virtual_set_out( mlt_playlist this )
{
	// Default producer to blank
	mlt_producer producer = &this->blank;

	// Map playlist position to real producer in virtual playlist
	mlt_timecode position = mlt_producer_position( &this->parent );

	// Loop through the virtual playlist
	int i = 0;

	for ( i = 0; i < this->count; i ++ )
	{
		if ( position < this->list[ i ]->playtime )
		{
			// Found it, now break
			producer = this->list[ i ]->producer;
			position += this->list[ i ]->in;
			break;
		}
		else
		{
			// Decrement position by length of this entry
			position -= this->list[ i ]->playtime;
		}
	}

	// Seek in real producer to relative position
	if ( i < this->count )
	{
		fprintf( stderr, "END OF CLIP %d AT %e\n", i, position );
		this->list[ i ]->playtime = position - this->list[ i ]->in;
	}

	return producer;
}

static int mlt_playlist_current_clip( mlt_playlist this )
{
	// Map playlist position to real producer in virtual playlist
	mlt_timecode position = mlt_producer_position( &this->parent );

	// Loop through the virtual playlist
	int i = 0;

	for ( i = 0; i < this->count; i ++ )
	{
		if ( position < this->list[ i ]->playtime )
		{
			// Found it, now break
			break;
		}
		else
		{
			// Decrement position by length of this entry
			position -= this->list[ i ]->playtime;
		}
	}

	return i;
}

/** Get the timecode which corresponds to the start of the next clip.
*/

mlt_timecode mlt_playlist_clip( mlt_playlist this, mlt_whence whence, int index )
{
	mlt_timecode position = 0;
	int absolute_clip = index;
	int i = 0;

	// Determine the absolute clip
	switch ( whence )
	{
		case mlt_whence_relative_start:
			absolute_clip = index;
			break;

		case mlt_whence_relative_current:
			absolute_clip = mlt_playlist_current_clip( this ) + index;
			break;

		case mlt_whence_relative_end:
			absolute_clip = this->count - index;
			break;
	}

	// Check that we're in a valid range
	if ( absolute_clip < 0 )
		absolute_clip = 0;
	else if ( absolute_clip > this->count )
		absolute_clip = this->count;

	// Now determine the timecode
	for ( i = 0; i < absolute_clip; i ++ )
		position += this->list[ i ]->playtime;

	return position;
}

/** Append a producer to the playlist.
*/

int mlt_playlist_append( mlt_playlist this, mlt_producer producer )
{
	// Append to virtual list
	return mlt_playlist_virtual_append( this, producer, 0, mlt_producer_get_playtime( producer ) );
}

/** Append a blank to the playlist of a given length.
*/

int mlt_playlist_blank( mlt_playlist this, mlt_timecode length )
{
	// Append to the virtual list
	return mlt_playlist_virtual_append( this, &this->blank, 0, length );
}

/** Get the current frame.
*/

static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index )
{
	// Get this mlt_playlist
	mlt_playlist this = producer->child;

	// Get the real producer
	mlt_producer real = mlt_playlist_virtual_seek( this );

	// Get the frame
	mlt_service_get_frame( mlt_producer_service( real ), frame, index );

	// Check if we're at the end of the clip
	mlt_properties properties = mlt_frame_properties( *frame );
	if ( mlt_properties_get_int( properties, "end_of_clip" ) )
		mlt_playlist_virtual_set_out( this );

	// Update timecode on the frame we're creating
	mlt_frame_set_timecode( *frame, mlt_producer_position( producer ) );

	// Position ourselves on the next frame
	mlt_producer_prepare_next( producer );

	return 0;
}

/** Close the playlist.
*/

void mlt_playlist_close( mlt_playlist this )
{
	mlt_producer_close( &this->parent );
	mlt_producer_close( &this->blank );
	free( this );
}
