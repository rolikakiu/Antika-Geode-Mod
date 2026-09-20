import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.GradientPaint;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Polygon;
import java.awt.Rectangle;
import java.awt.RenderingHints;
import java.awt.event.ActionEvent;
import java.awt.event.KeyEvent;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import javax.swing.AbstractAction;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.KeyStroke;
import javax.swing.SwingUtilities;
import javax.swing.Timer;

public class geometrydash extends JPanel {
    private static final int WIDTH = 900;
    private static final int HEIGHT = 520;
    private static final int GROUND_Y = 420;
    private static final int PLAYER_SIZE = 40;

    private final Timer timer;
    private final Random random = new Random();
    private final List<Obstacle> obstacles = new ArrayList<>();

    private double playerY = GROUND_Y - PLAYER_SIZE;
    private double velocityY = 0;
    private double rotation = 0;
    private int playerX = 120;
    private int tick = 0;
    private int score = 0;
    private int highScore = 0;
    private int speed = 7;
    private boolean running = false;
    private boolean gameOver = false;
    private boolean paused = false;
    private boolean settingsOpen = false;
    private boolean showGrid = true;
    private boolean showHitboxes = true;

    private final Rectangle resumeButton = new Rectangle(WIDTH / 2 - 120, HEIGHT / 2 - 10, 240, 48);
    private final Rectangle settingsButton = new Rectangle(WIDTH / 2 - 120, HEIGHT / 2 + 50, 240, 48);
    private final Rectangle restartButton = new Rectangle(WIDTH / 2 - 120, HEIGHT / 2 + 110, 240, 48);
    private final Rectangle gridButton = new Rectangle(WIDTH / 2 - 140, HEIGHT / 2 - 10, 280, 44);
    private final Rectangle hitboxButton = new Rectangle(WIDTH / 2 - 140, HEIGHT / 2 + 44, 280, 44);
    private final Rectangle backButton = new Rectangle(WIDTH / 2 - 140, HEIGHT / 2 + 98, 280, 44);

    public geometrydash() {
        setPreferredSize(new Dimension(WIDTH, HEIGHT));
        setBackground(new Color(10, 10, 18));
        setFocusable(true);

        bindControls();
        addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                if (handleMenuClick(e.getX(), e.getY())) {
                    return;
                }
                jumpOrStart();
            }
        });

        timer = new Timer(16, e -> updateGame());
        timer.start();
        resetLevel();
    }

    private void bindControls() {
        getInputMap(WHEN_IN_FOCUSED_WINDOW).put(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0), "jump");
        getInputMap(WHEN_IN_FOCUSED_WINDOW).put(KeyStroke.getKeyStroke(KeyEvent.VK_UP, 0), "jump");
        getInputMap(WHEN_IN_FOCUSED_WINDOW).put(KeyStroke.getKeyStroke(KeyEvent.VK_R, 0), "restart");
        getInputMap(WHEN_IN_FOCUSED_WINDOW).put(KeyStroke.getKeyStroke(KeyEvent.VK_P, 0), "pause");
        getInputMap(WHEN_IN_FOCUSED_WINDOW).put(KeyStroke.getKeyStroke(KeyEvent.VK_ESCAPE, 0), "mainMenu");

        getActionMap().put("jump", new AbstractAction() {
            @Override
            public void actionPerformed(ActionEvent e) {
                jumpOrStart();
            }
        });
        getActionMap().put("restart", new AbstractAction() {
            @Override
            public void actionPerformed(ActionEvent e) {
                resetLevel();
                running = true;
            }
        });
        getActionMap().put("pause", new AbstractAction() {
            @Override
            public void actionPerformed(ActionEvent e) {
                if (running && !gameOver) {
                    paused = !paused;
                    settingsOpen = false;
                }
            }
        });
        getActionMap().put("mainMenu", new AbstractAction() {
            @Override
            public void actionPerformed(ActionEvent e) {
                resetLevel();
            }
        });
    }

    private boolean handleMenuClick(int x, int y) {
        if (!paused || gameOver) {
            return false;
        }

        if (settingsOpen) {
            if (gridButton.contains(x, y)) {
                showGrid = !showGrid;
            } else if (hitboxButton.contains(x, y)) {
                showHitboxes = !showHitboxes;
            } else if (backButton.contains(x, y)) {
                settingsOpen = false;
            }
            repaint();
            return true;
        }

        if (resumeButton.contains(x, y)) {
            paused = false;
        } else if (settingsButton.contains(x, y)) {
            settingsOpen = true;
        } else if (restartButton.contains(x, y)) {
            resetLevel();
            running = true;
        }
        repaint();
        return true;
    }

    private void jumpOrStart() {
        if (gameOver) {
            resetLevel();
            running = true;
            return;
        }
        if (!running) {
            running = true;
        }
        if (!paused && onGround()) {
            velocityY = -15.5;
        }
    }

    private void resetLevel() {
        obstacles.clear();
        playerY = GROUND_Y - PLAYER_SIZE;
        velocityY = 0;
        rotation = 0;
        tick = 0;
        score = 0;
        speed = 7;
        running = false;
        gameOver = false;
        paused = false;
        settingsOpen = false;
        spawnObstacle(WIDTH + 180);
        spawnObstacle(WIDTH + 480);
        repaint();
    }

    private void updateGame() {
        if (!running || paused || gameOver) {
            repaint();
            return;
        }

        tick++;
        velocityY += 0.8;
        playerY += velocityY;

        if (playerY >= GROUND_Y - PLAYER_SIZE) {
            playerY = GROUND_Y - PLAYER_SIZE;
            velocityY = 0;
            rotation = Math.round(rotation / 90.0) * 90.0;
        } else {
            rotation += 7.5;
        }

        Iterator<Obstacle> iterator = obstacles.iterator();
        while (iterator.hasNext()) {
            Obstacle obstacle = iterator.next();
            obstacle.x -= speed;
            if (!obstacle.passed && obstacle.x + obstacle.width < playerX) {
                obstacle.passed = true;
                score++;
                highScore = Math.max(highScore, score);
                speed = Math.min(13, 7 + score / 6);
            }
            if (obstacle.x + obstacle.width < -40) {
                iterator.remove();
            }
        }

        if (obstacles.isEmpty() || obstacles.get(obstacles.size() - 1).x < WIDTH - 240) {
            spawnObstacle(WIDTH + 80 + random.nextInt(170));
        }

        if (collides()) {
            gameOver = true;
            running = false;
        }

        repaint();
    }

    private void spawnObstacle(int x) {
        int size = 34 + random.nextInt(16);
        obstacles.add(new Obstacle(x, GROUND_Y - size, size, size));
    }

    private boolean collides() {
        int px = playerX + 5;
        int py = (int) playerY + 5;
        int ps = PLAYER_SIZE - 10;

        for (Obstacle obstacle : obstacles) {
            Rectangle hitbox = obstacleHitbox(obstacle);
            if (px < hitbox.x + hitbox.width
                    && px + ps > hitbox.x
                    && py < hitbox.y + hitbox.height
                    && py + ps > hitbox.y) {
                return true;
            }
        }
        return false;
    }

    private Rectangle obstacleHitbox(Obstacle obstacle) {
        int width = Math.max(14, obstacle.width / 3);
        int height = Math.max(22, obstacle.height * 2 / 3);
        int x = obstacle.x + (obstacle.width - width) / 2;
        int y = obstacle.y + obstacle.height / 4;
        return new Rectangle(x, y, width, height);
    }

    private boolean onGround() {
        return playerY >= GROUND_Y - PLAYER_SIZE - 0.5;
    }

    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        Graphics2D g2 = (Graphics2D) g.create();
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        drawBackground(g2);
        drawGround(g2);
        drawObstacles(g2);
        drawPlayer(g2);
        drawHud(g2);
        drawOverlay(g2);

        g2.dispose();
    }

    private void drawBackground(Graphics2D g2) {
        g2.setPaint(new GradientPaint(0, 0, new Color(8, 8, 20), 0, HEIGHT, new Color(20, 10, 38)));
        g2.fillRect(0, 0, WIDTH, HEIGHT);

        if (showGrid) {
            g2.setColor(new Color(0, 229, 255, 28));
            for (int x = -(tick * speed / 67) % 60; x < WIDTH; x += 60) {
                g2.drawLine(x, 0, x, HEIGHT);
            }
            for (int y = 0; y < HEIGHT; y += 60) {
                g2.drawLine(0, y, WIDTH, y);
            }
        }
    }

    private void drawGround(Graphics2D g2) {
        g2.setColor(new Color(255, 255, 255, 35));
        g2.setStroke(new BasicStroke(3));
        g2.drawLine(0, GROUND_Y, WIDTH, GROUND_Y);
        g2.setColor(new Color(0, 229, 255, 70));
        for (int x = -(tick * speed) % 40; x < WIDTH; x += 40) {
            g2.drawLine(x, GROUND_Y, x + 26, HEIGHT);
        }
    }

    private void drawObstacles(Graphics2D g2) {
        for (Obstacle obstacle : obstacles) {
            Polygon spike = new Polygon();
            spike.addPoint(obstacle.x, obstacle.y + obstacle.height);
            spike.addPoint(obstacle.x + obstacle.width / 2, obstacle.y);
            spike.addPoint(obstacle.x + obstacle.width, obstacle.y + obstacle.height);

            g2.setColor(new Color(255, 110, 199, 70));
            g2.fillPolygon(spike);
            g2.setColor(new Color(255, 110, 199));
            g2.setStroke(new BasicStroke(3));
            g2.drawPolygon(spike);

            if (showHitboxes) {
                Rectangle hitbox = obstacleHitbox(obstacle);
                g2.setColor(new Color(255, 0, 0, 75));
                g2.fillRect(hitbox.x, hitbox.y, hitbox.width, hitbox.height);
                g2.setColor(new Color(255, 40, 40));
                g2.setStroke(new BasicStroke(4));
                g2.drawRect(hitbox.x, hitbox.y, hitbox.width, hitbox.height);
            }
        }
    }

    private void drawPlayer(Graphics2D g2) {
        Graphics2D p = (Graphics2D) g2.create();
        int cx = playerX + PLAYER_SIZE / 2;
        int cy = (int) playerY + PLAYER_SIZE / 2;
        p.rotate(Math.toRadians(rotation), cx, cy);
        p.setColor(new Color(0, 229, 255, 70));
        p.fillRoundRect(playerX - 4, (int) playerY - 4, PLAYER_SIZE + 8, PLAYER_SIZE + 8, 10, 10);
        p.setColor(new Color(0, 229, 255));
        p.fillRoundRect(playerX, (int) playerY, PLAYER_SIZE, PLAYER_SIZE, 8, 8);
        p.setColor(new Color(255, 208, 0));
        p.fillOval(playerX + 9, (int) playerY + 10, 8, 8);
        p.fillOval(playerX + 24, (int) playerY + 10, 8, 8);
        p.setColor(new Color(8, 8, 20));
        p.fillRect(playerX + 12, (int) playerY + 27, 18, 4);
        p.dispose();

        if (showHitboxes) {
            g2.setColor(new Color(255, 208, 0, 160));
            g2.drawRect(playerX + 5, (int) playerY + 5, PLAYER_SIZE - 10, PLAYER_SIZE - 10);
        }
    }

    private void drawHud(Graphics2D g2) {
        g2.setFont(new Font("Arial", Font.BOLD, 20));
        g2.setColor(Color.WHITE);
        g2.drawString("Score: " + score, 24, 34);
        g2.drawString("Best: " + highScore, 24, 62);

        g2.setFont(new Font("Arial", Font.PLAIN, 13));
        g2.setColor(new Color(185, 185, 205));
        g2.drawString("Space/Click = jump   P = pause   R = restart   Esc = menu", 24, 88);
    }

    private void drawOverlay(Graphics2D g2) {
        String title = null;
        String subtitle = null;

        if (gameOver) {
            title = "CRASHED";
            subtitle = "Click, Space, or R to restart";
        } else if (paused) {
            drawPauseMenu(g2);
            return;
        } else if (!running) {
            title = "GEOMETRY DASH";
            subtitle = "Click or press Space to start";
        }

        if (title == null) {
            return;
        }

        g2.setColor(new Color(0, 0, 0, 130));
        g2.fillRect(0, 0, WIDTH, HEIGHT);
        drawCentered(g2, title, 78, HEIGHT / 2 - 28, new Color(0, 229, 255));
        drawCentered(g2, subtitle, 22, HEIGHT / 2 + 28, Color.WHITE);
    }

    private void drawPauseMenu(Graphics2D g2) {
        g2.setColor(new Color(0, 0, 0, 155));
        g2.fillRect(0, 0, WIDTH, HEIGHT);

        if (settingsOpen) {
            drawCentered(g2, "SETTINGS", 64, HEIGHT / 2 - 76, new Color(0, 229, 255));
            drawButton(g2, gridButton, "Grid: " + (showGrid ? "ON" : "OFF"));
            drawButton(g2, hitboxButton, "Hitboxes: " + (showHitboxes ? "ON" : "OFF"));
            drawButton(g2, backButton, "Back");
            return;
        }

        drawCentered(g2, "PAUSED", 72, HEIGHT / 2 - 76, new Color(0, 229, 255));
        drawButton(g2, resumeButton, "Resume");
        drawButton(g2, settingsButton, "Settings");
        drawButton(g2, restartButton, "Restart");
    }

    private void drawButton(Graphics2D g2, Rectangle button, String text) {
        g2.setColor(new Color(20, 20, 36, 230));
        g2.fillRoundRect(button.x, button.y, button.width, button.height, 14, 14);
        g2.setColor(new Color(0, 229, 255));
        g2.setStroke(new BasicStroke(2));
        g2.drawRoundRect(button.x, button.y, button.width, button.height, 14, 14);

        g2.setFont(new Font("Arial", Font.BOLD, 22));
        FontMetrics metrics = g2.getFontMetrics();
        int textX = button.x + (button.width - metrics.stringWidth(text)) / 2;
        int textY = button.y + (button.height + metrics.getAscent() - metrics.getDescent()) / 2;
        g2.setColor(Color.WHITE);
        g2.drawString(text, textX, textY);
    }

    private void drawCentered(Graphics2D g2, String text, int size, int y, Color color) {
        g2.setFont(new Font("Arial", Font.BOLD, size));
        FontMetrics metrics = g2.getFontMetrics();
        int x = (WIDTH - metrics.stringWidth(text)) / 2;
        g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), 80));
        g2.drawString(text, x + 3, y + 3);
        g2.setColor(color);
        g2.drawString(text, x, y);
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("Geometry Dash Java");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setResizable(false);
            frame.setContentPane(new geometrydash());
            frame.pack();
            frame.setLocationRelativeTo(null);
            frame.setVisible(true);
        });
    }

    private static class Obstacle {
        int x;
        int y;
        int width;
        int height;
        boolean passed;

        Obstacle(int x, int y, int width, int height) {
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
        }
    }
}
