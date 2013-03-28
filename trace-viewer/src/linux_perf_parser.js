// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Base class for linux perf event parsers.
 *
 * The linux perf trace event importer depends on subclasses of
 * LinuxPerfParser to parse event data.  Each subclass corresponds
 * to a group of trace events; e.g. LinuxPerfSchedParser implements
 * parsing of sched:* kernel trace events.  Parser subclasses must
 * call LinuxPerfParser.registerSubtype to arrange to be instantiated
 * and their constructor must register their event handlers with the
 * importer.  For example,
 *
 * var LinuxPerfParser = tracing.LinuxPerfParser;
 *
 * function LinuxPerfWorkqueueParser(importer) {
 *   LinuxPerfParser.call(this, importer);
 *
 *   importer.registerEventHandler('workqueue_execute_start',
 *       LinuxPerfWorkqueueParser.prototype.executeStartEvent.bind(this));
 *   importer.registerEventHandler('workqueue_execute_end',
 *       LinuxPerfWorkqueueParser.prototype.executeEndEvent.bind(this));
 * }
 *
 * LinuxPerfParser.registerSubtype(LinuxPerfWorkqueueParser);
 *
 * When a registered event name is found in the data stream the associated
 * event handler is invoked:
 *
 *   executeStartEvent: function(eventName, cpuNumber, ts, eventBase)
 *
 * If the routine returns false the caller will generate an import error
 * saying there was a problem parsing it.  Handlers can also emit import
 * messages using this.importer.importError.  If this is done in lieu of
 * the generic import error it may be desirable for the handler to return
 * true.
 *
 * Trace events generated by writing to the trace_marker file are expected
 * to have a leading text marker followed by a ':'; e.g. the trace clock
 * synchronization event is:
 *
 *  tracing_mark_write: trace_event_clock_sync: parent_ts=0
 *
 * To register an event handler for these events, prepend the marker with
 * 'tracing_mark_write:'; e.g.
 *
 *    this.registerEventHandler('tracing_mark_write:trace_event_clock_sync',
 *
 * All subclasses should depend on linux_perf_parser, e.g.
 *
 * base.defineModule('linux_perf_workqueue_parser')
 *   .dependsOn('linux_perf_parser')
 *   .exportsTo('tracing', function()
 *
 * and be listed in the dependsOn of LinuxPerfImporter.  Beware that after
 * adding a new subclass you must run build/generate_about_tracing_contents.py
 * to regenerate about_tracing.*.
 */
base.exportTo('tracing', function() {

  var subtypeConstructors = [];

  /**
   * Registers a subclass that will help parse linux perf events.
   * The importer will call createParsers (below) before importing
   * data so each subclass can register its handlers.
   *
   * @param {Function} subtypeConstructor The subtype's constructor function.
   */
  LinuxPerfParser.registerSubtype = function(subtypeConstructor) {
    subtypeConstructors.push(subtypeConstructor);
  };

  LinuxPerfParser.getSubtypeConstructors = function() {
    return subtypeConstructors;
  };

  /**
   * Parses linux perf events.
   * @constructor
   */
  function LinuxPerfParser(importer) {
    this.importer = importer;
    this.model = importer.model;
  }

  LinuxPerfParser.prototype = {
    __proto__: Object.prototype
  };

  return {
    LinuxPerfParser: LinuxPerfParser
  };

});
