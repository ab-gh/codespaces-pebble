const webpack = require('webpack');

module.exports = {
  plugins: [
    new webpack.DefinePlugin({
      'WEATHER_API_KEY': JSON.stringify(process.env.WEATHER_SECRET || '')
    })
  ]
};
