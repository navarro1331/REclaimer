/* Guilty Spark 343 native host adapter. The compatible UI retains its MIT notice. */
'use strict';
window.sparkModel = decodeURIComponent(location.pathname.split('/')[2] || '');
window.sparkPrefix = '/m/' + encodeURIComponent(window.sparkModel);
window.sparkFetch = window.fetch.bind(window);
window.fetch = function(input, options) {
  const url = new URL(typeof input === 'string' ? input : input.url, location.href);
  if (url.origin === location.origin && !url.pathname.startsWith('/m/') && !['/auth','/models','/'].includes(url.pathname)) {
    url.pathname = window.sparkPrefix + url.pathname;
    return window.sparkFetch(url.href, options);
  }
  return window.sparkFetch(input, options);
};
