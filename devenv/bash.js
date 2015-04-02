/*
 * Copyright (c) 2013 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

NaClTerm.nmf = 'bash.nmf';
NaClTerm.argv = ['--init-file', '/mnt/http/bashrc'];
NaClTerm.env = ['ID='+frameElement.id];
// TODO(bradnelson): Drop this hack once tar extraction first checks relative
// to the nexe.
NaClProcessManager.useNaClAltHttp = true;

function onInit() {
  // Request 2GB storage.
  navigator.webkitPersistentStorage.requestQuota(
      2 * 1024 * 1024 * 1024,
      NaClTerm.init,
      function() {
        console.log("Failed to allocate space!\n");
        // Start the terminal even if FS failed to init.
        NaClTerm.init();
      });
}

window.onload = function() {
  lib.init(function() {
    onInit();
  });
};
