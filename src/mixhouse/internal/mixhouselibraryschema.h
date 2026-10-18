/*
 * GlideMix
 *
 * The library schema: Mixxx's tables (schema version 39, the one GlideMix
 * reads and writes) and GlideMix's own. A new library starts from this when
 * there is no Mixxx library to import.
 */
#pragma once

namespace au::mixhouse {
inline constexpr const char* kLibrarySchemaVersion = "39";

inline constexpr const char* kLibrarySchema = R"SQL(
CREATE TABLE settings (
        name TEXT UNIQUE NOT NULL,
        value TEXT,
        locked INTEGER DEFAULT 0,
        hidden INTEGER DEFAULT 0);
CREATE TABLE track_locations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        location varchar(512) UNIQUE,
        filename varchar(512),
        directory varchar(512),
        filesize INTEGER,
        fs_deleted INTEGER,
        needs_verification INTEGER);
CREATE TABLE LibraryHashes (
        directory_path VARCHAR(256) PRIMARY KEY,
        hash INTEGER,
        directory_deleted INTEGER, needs_verification INTEGER DEFAULT 0);
CREATE TABLE Playlists (
        id INTEGER PRIMARY KEY,
        name varchar(48),
        position INTEGER,
        hidden INTEGER DEFAULT 0 NOT NULL,
        date_created datetime,
        date_modified datetime, locked INTEGER DEFAULT 0, parent_id INTEGER DEFAULT -1 NOT NULL, is_folder INTEGER DEFAULT 0 NOT NULL, sort_order INTEGER DEFAULT -1 NOT NULL);
CREATE TABLE PlaylistTracks (
        id INTEGER PRIMARY KEY,
        playlist_id INTEGER REFERENCES Playlists(id),
        track_id INTEGER REFERENCES "library_old"(id),
        position INTEGER, pl_datetime_added);
CREATE TABLE cues (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        track_id INTEGER NOT NULL REFERENCES "library_old"(id),
        type INTEGER DEFAULT 0 NOT NULL,
        
        position INTEGER DEFAULT -1 NOT NULL,
        length INTEGER DEFAULT 0 NOT NULL,
        hotcue INTEGER DEFAULT -1 NOT NULL,
        label TEXT DEFAULT '' NOT NULL, color INTEGER DEFAULT 4294901760 NOT NULL);
CREATE TABLE crates (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name varchar(48) UNIQUE NOT NULL,
        count INTEGER DEFAULT 0,
        show INTEGER DEFAULT 1, locked INTEGER DEFAULT 0, autodj_source INTEGER DEFAULT 0);
CREATE TABLE crate_tracks (
        crate_id INTEGER NOT NULL REFERENCES crates(id),
        track_id INTEGER NOT NULL REFERENCES "library_old"(id),
        UNIQUE (crate_id, track_id));
CREATE TABLE library (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        artist varchar(64),
        title varchar(64),
        album varchar(64),
        year varchar(16),
        genre varchar(64),
        tracknumber varchar(3),
        location INTEGER REFERENCES track_locations(location),
        comment varchar(256),
        url varchar(256),
        duration FLOAT,
        bitrate INTEGER,
        samplerate INTEGER,
        cuepoint INTEGER,
        bpm FLOAT,
        wavesummaryhex BLOB,
        channels INTEGER,
        datetime_added DEFAULT CURRENT_TIMESTAMP,
        mixxx_deleted INTEGER,
        played INTEGER,
        header_parsed INTEGER DEFAULT 0, filetype varchar(8) DEFAULT '?', replaygain FLOAT DEFAULT 0, timesplayed INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, key varchar(8) DEFAULT '', beats BLOB, beats_version TEXT, composer varchar(64) DEFAULT '', bpm_lock INTEGER DEFAULT 0, beats_sub_version TEXT DEFAULT '', keys BLOB, keys_version TEXT, keys_sub_version TEXT, key_id INTEGER DEFAULT 0, grouping TEXT DEFAULT '', album_artist TEXT DEFAULT '', coverart_source INTEGER DEFAULT 0, coverart_type INTEGER DEFAULT 0, coverart_location TEXT DEFAULT '', coverart_hash INTEGER DEFAULT 0, replaygain_peak REAL DEFAULT -1.0, tracktotal TEXT DEFAULT '//', color INTEGER, coverart_color INTEGER, coverart_digest BLOB, last_played_at DATETIME DEFAULT NULL, source_synchronized_ms INTEGER DEFAULT NULL);
CREATE TABLE itunes_library (
        id INTEGER PRIMARY KEY,
        artist varchar(48), title varchar(48),
        album varchar(48), year varchar(16),
        genre varchar(32), tracknumber varchar(3),
        location varchar(512),
        comment varchar(60),
        duration INTEGER,
        bitrate INTEGER,
        bpm INTEGER,
        rating INTEGER, grouping TEXT DEFAULT '', album_artist TEXT DEFAULT '');
CREATE TABLE itunes_playlists (
        id INTEGER PRIMARY KEY,
        name varchar(100) UNIQUE);
CREATE TABLE itunes_playlist_tracks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        playlist_id INTEGER REFERENCES itunes_playlist(id),
        track_id INTEGER REFERENCES itunes_library(id), position INTEGER DEFAULT 0);
CREATE TABLE traktor_library (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        artist varchar(48), title varchar(48),
        album varchar(48), year varchar(16),
        genre varchar(32), tracknumber varchar(3),
        location varchar(512) UNIQUE,
        comment varchar(60),
        duration INTEGER,
        bitrate INTEGER,
        bpm FLOAT,
        key varchar(6),
        rating INTEGER
        );
CREATE TABLE traktor_playlists (
        id INTEGER PRIMARY KEY,
        name varchar(100) UNIQUE
        );
CREATE TABLE traktor_playlist_tracks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        playlist_id INTEGER REFERENCES traktor_playlist(id),
        track_id INTEGER REFERENCES traktor_library(id)
        , position INTEGER DEFAULT 0);
CREATE TABLE rhythmbox_library (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        artist varchar(48), title varchar(48),
        album varchar(48), year varchar(16),
        genre varchar(32), tracknumber varchar(3),
        location varchar(512) UNIQUE,
        comment varchar(60),
        duration INTEGER,
        bitrate INTEGER,
        bpm FLOAT,
        key varchar(6),
        rating INTEGER
        );
CREATE TABLE rhythmbox_playlists (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name varchar(100) UNIQUE
        );
CREATE TABLE rhythmbox_playlist_tracks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        playlist_id INTEGER REFERENCES rhythmbox_playlist(id),
        track_id INTEGER REFERENCES rhythmbox_library(id)
        , position INTEGER DEFAULT 0);
CREATE TABLE track_analysis (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      track_id INTEGER NOT NULL REFERENCES track_locations(id),
      type varchar(512),
      description varchar(1024),
      version varchar(512),
      created DEFAULT CURRENT_TIMESTAMP,
      data_checksum varchar(512)
    );
CREATE INDEX track_analysis_track_id_index ON track_analysis (track_id);
CREATE TABLE directories (
        directory TEXT UNIQUE
      );
CREATE INDEX idx_PlaylistTracks_playlist_id_track_id ON PlaylistTracks (
          playlist_id,
          track_id
      );
CREATE INDEX idx_PlaylistTracks_track_id ON PlaylistTracks (
          track_id
      );
CREATE INDEX idx_crate_tracks_track_id ON crate_tracks (
          track_id
      );
CREATE TABLE mixhouse_downbeats ( track_id INTEGER PRIMARY KEY, downbeat_sec REAL NOT NULL, confidence REAL NOT NULL, detector TEXT NOT NULL);
CREATE TABLE mixhouse_anchors ( location TEXT PRIMARY KEY, intro_sec REAL NOT NULL, outro_sec REAL NOT NULL);
CREATE TABLE mixhouse_phrases ( track_id INTEGER PRIMARY KEY, phrases TEXT NOT NULL);
CREATE INDEX mixhouse_idx_track_locations_filename_size ON track_locations (filename, filesize);
)SQL";
}
