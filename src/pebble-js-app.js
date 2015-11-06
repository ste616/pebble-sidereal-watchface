Pebble.addEventListener('showConfiguration', function(e) {
  // Show config page
  Pebble.openURL('http://astrowebservices.com/~ste616/tools/cityLongitude/index.html');
});

Pebble.addEventListener('webviewclosed', function(e) {
  var config_data = JSON.parse(decodeURIComponent(e.response));
  console.log('Config window returned: ', JSON.stringify(config_data));

  // Prepare AppMessage payload
  var dict = {
    'LONGITUDE': config_data['longitude']
  };

  // Send settings to Pebble watchapp
  Pebble.sendAppMessage(dict, function(){
    console.log('Sent config data to Pebble');  
  }, function() {
    console.log('Failed to send config data!');
  });
});

