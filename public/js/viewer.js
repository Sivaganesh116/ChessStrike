document.addEventListener('DOMContentLoaded', async () => {
    const boardElementId = 'board';
    const movesTbody = document.getElementById('moves-tbody');
    const prevBtn = document.getElementById('prev-btn');
    const nextBtn = document.getElementById('next-btn');

    let board = null;
    let game = new Chess();
    let moveHistory = []; // FEN history
    let timeStamps = [];
    let currentMoveIndex = 0;

    function updateNavButtons() {
        prevBtn.disabled = currentMoveIndex <= 0;
        nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
    }

    function updateTimersAt(index) {
        const ts = timeStamps[index] || [600, 600];
        document.getElementById('white-timer')?.innerText = formatTime(ts[0]);
        document.getElementById('black-timer')?.innerText = formatTime(ts[1]);
    }

    function formatTime(seconds) {
        const m = Math.floor(seconds / 60);
        const s = seconds % 60;
        return `${m}:${s.toString().padStart(2, '0')}`;
    }

    function updateMoveHistory(san, color, fen, index) {
        moveHistory.push(fen);
        const moveNumber = Math.ceil(moveHistory.length / 2);
        if (color === 'w') {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td class="p-1">${moveNumber}</td>
                <td class="p-1 font-semibold cursor-pointer" data-move-index="${index}">${san}</td>
                <td class="p-1 font-semibold"></td>
            `;
            movesTbody.appendChild(row);
        } else {
            const lastRow = movesTbody.lastElementChild;
            if (lastRow) {
                const cell = lastRow.children[2];
                cell.textContent = san;
                cell.classList.add('cursor-pointer');
                cell.dataset.moveIndex = index;
            }
        }
    }

    function insertGameResultBlock(result, reason, whiteUsername, blackUsername) {
        const rightPanel = document.querySelector('.bg-white.p-4.rounded-lg.shadow-lg.flex.flex-col');
        if (!rightPanel) return;

        const resultDiv = document.createElement('div');
        resultDiv.className = 'mt-4 p-4 bg-yellow-100 border-l-4 border-yellow-500 rounded shadow';

        const title = document.createElement('h3');
        title.className = 'text-xl font-bold mb-2 text-yellow-800';
        title.textContent = 'Game Result';
        resultDiv.appendChild(title);

        const resultPara = document.createElement('p');
        resultPara.className = 'text-gray-800 font-medium';

        if (result === "draw") {
            resultPara.textContent = `Draw by ${reason}`;
        } else if (result === "white") {
            resultPara.textContent = `${whiteUsername} won by ${reason}`;
        } else if (result === "black") {
            resultPara.textContent = `${blackUsername} won by ${reason}`;
        } else {
            resultPara.textContent = `Result unavailable`;
        }

        resultDiv.appendChild(resultPara);

        // Insert before first <hr> in right panel (usually above Actions)
        const allChildren = rightPanel.children;
        for (let i = 0; i < allChildren.length; i++) {
            if (allChildren[i].tagName === 'HR') {
                rightPanel.insertBefore(resultDiv, allChildren[i]);
                return;
            }
        }

        rightPanel.appendChild(resultDiv); // fallback
    }


    function setupEventListeners() {
        prevBtn.addEventListener('click', () => {
            if (currentMoveIndex > 0) {
                currentMoveIndex--;
                board.position(moveHistory[currentMoveIndex], false);
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
            }
        });

        nextBtn.addEventListener('click', () => {
            if (currentMoveIndex < moveHistory.length - 1) {
                currentMoveIndex++;
                board.position(moveHistory[currentMoveIndex], false);
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
            }
        });

        movesTbody.addEventListener('click', (e) => {
            const target = e.target.closest('[data-move-index]');
            if (target) {
                const idx = parseInt(target.dataset.moveIndex, 10);
                if (!isNaN(idx)) {
                    currentMoveIndex = idx;
                    board.position(moveHistory[idx], false);
                    updateTimersAt(idx);
                    updateNavButtons();
                }
            }
        });
    }

    function initBoard() {
        const config = {
            draggable: false,
            position: 'start',
            pieceTheme: '/img/chesspieces/wikipedia/{piece}.png',
        };
        board = Chessboard(boardElementId, config);
    }

    async function loadGame() {
        const res = await fetch('/game/info');
        if (!res.ok) throw new Error('Failed to fetch game info');
        const gameData = await res.json();

        const { white, black, result, reason } = gameData;
        insertGameResultBlock(result, reason, white, black);


        // Display player names
        const whiteNameElem = document.getElementById('opponent-name');
        const blackNameElem = document.getElementById('player-name');
        whiteNameElem.innerHTML = `<a href="/player/${gameData.white}" class="text-blue-600 hover:underline">${gameData.white}</a>`;
        blackNameElem.innerHTML = `<a href="/player/${gameData.black}" class="text-blue-600 hover:underline">${gameData.black}</a>`;

        // Initialize game state
        game = new Chess();
        moveHistory = [game.fen()]; // start pos
        timeStamps = [[600, 600], ...gameData.timeStamps]; // add initial time
        currentMoveIndex = 0;

        // Apply moves
        gameData.moves.forEach((uci, idx) => {
            const move = game.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: 'q' });
            if (!move) return;

            const fen = game.fen();
            updateMoveHistory(move.san, move.color, fen, idx + 1);
        });

        board.position(moveHistory[0], false);
        updateTimersAt(0);
        updateNavButtons();
    }

    initBoard();
    setupEventListeners();
    await loadGame();
});
