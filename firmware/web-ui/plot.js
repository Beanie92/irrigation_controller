import { createChart, LineSeries } from 'lightweight-charts';
import './src/style.css';

document.addEventListener('DOMContentLoaded', () => {
  console.log("Plot script started");

  const chartContainer = document.getElementById('chart-container');
  console.log("Chart container:", chartContainer);
  if (chartContainer) {
    console.log("Chart container clientWidth:", chartContainer.clientWidth);
  }

  const chart = createChart(chartContainer, {
    width: chartContainer.clientWidth,
    height: 300,
    layout: {
      backgroundColor: '#ffffff',
      textColor: 'rgba(33, 56, 77, 1)',
    },
    grid: {
      vertLines: {
        color: 'rgba(197, 203, 206, 0.5)',
      },
      horzLines: {
        color: 'rgba(197, 203, 206, 0.5)',
      },
    },
    timeScale: {
      timeVisible: true,
      secondsVisible: true,
    },
  });

  const lineSeries = chart.addSeries(LineSeries, {
    color: 'rgba(75, 192, 192, 1)',
    lineWidth: 2,
  });

  const data = [];

  function fetchData() {
    console.log("Fetching data...");
    fetch('/api/current')
      .then(response => response.json())
      .then(apiData => {
        console.log("Data received:", apiData);
        const now = new Date();
        const newDataPoint = {
          time: now.getTime() / 1000,
          value: apiData.current,
        };
        data.push(newDataPoint);

        if (data.length > 30) {
          data.shift();
        }

        lineSeries.setData(data);
      })
      .catch(err => console.error('Error fetching current data:', err));
  }

  setInterval(fetchData, 2000);
});
