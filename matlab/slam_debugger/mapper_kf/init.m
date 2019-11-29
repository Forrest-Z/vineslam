function L = init(N_x, N_y)
    % this function initializes a map of landmarks
    % - N_x: number of vines per corridor
    % - N_y: number of vineyard corridors
    
    x_inc   = 1; % trunks spaced of 1 meter
    y_inc   = 2; % vineyard of lenght 2 meters
    
    c_index = (1-N_y)*y_inc+1:y_inc:(N_y-1)*y_inc;  % indexes of each corridor
    
    for i = 1:length(c_index)
        for j = 1:N_x
            L(i,j,:) = [-round(N_x/2) + j * x_inc,c_index(i)];
        end
    end
end